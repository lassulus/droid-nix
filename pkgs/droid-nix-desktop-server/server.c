/*
 * droid-nix-desktop-server: shows a wlroots compositor's output in the app's
 * "droid-nix desktop" (DesktopActivity) and passes its touches and keys back.
 *
 * It is a Wayland client of the compositor and serves the app on the unix
 * socket $DROID_NIX_DESKTOP_SOCKET. For each frame the app asks for, the
 * compositor copies its first output (wlr-screencopy, once something
 * changed) into a memfd that the app maps as well, so the socket only
 * carries small messages. Touch and keys come back as messages and go to
 * the compositor through a virtual pointer and keyboard.
 *
 * Messages are native-endian int32s.
 * Server to app, 5 each:
 *   0 width height stride format  the frame buffer, a memfd sent along with
 *                                 it (SCM_RIGHTS); format is a wl_shm format
 *   1 x y width height            the next frame is in the buffer, changed
 *                                 in this box
 * App to server, 4 each:
 *   0 0 0 0                       copy the next frame
 *   1 pressed x y                 the pointer, in buffer pixels, and whether
 *                                 the left button is pressed
 *   2 pressed keysym 0            a key
 *   3 width height 0              make the output this size
 */
#define _GNU_SOURCE
#include <errno.h>
#include <linux/input-event-codes.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include "wlr-output-management-unstable-v1-client-protocol.h"
#include "wlr-screencopy-unstable-v1-client-protocol.h"
#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"

static struct wl_display *display;
static struct wl_registry *registry;
static struct wl_shm *shm;
static struct wl_seat *seat;
static struct wl_output *output;
static uint32_t screencopy_name;
static struct zwlr_screencopy_manager_v1 *screencopy;
static struct zwlr_virtual_pointer_manager_v1 *pointer_manager;
static uint32_t pointer_manager_version;
static struct zwlr_virtual_pointer_v1 *pointer;
static struct zwp_virtual_keyboard_manager_v1 *keyboard_manager;
static struct zwp_virtual_keyboard_v1 *keyboard;
static struct zwlr_output_manager_v1 *output_manager;
static struct zwlr_output_head_v1 *head;
static uint32_t output_serial;
static int32_t output_width, output_height;

// The app, -1 if none.
static int client = -1;
static int32_t message[4];
static size_t message_size;

// The frame buffer, shared with the app.
static int fb = -1;
static struct wl_buffer *buffer;
static uint32_t fb_format, fb_width, fb_height, fb_stride;

// The capture in progress, whether the app waits for one, and whether the
// last one failed (it is retried after the output's next change).
static struct zwlr_screencopy_frame_v1 *frame;
static bool wanted, failed, full;
static int32_t damage_x1, damage_y1, damage_x2, damage_y2;

static bool button;
// The keymap: keysyms[k] is the keysym of keycode k, for 8 <= k < 8 + keys.
static xkb_keysym_t keysyms[256];
static int keys;
static bool pressed[256];
// Virtual keyboards' keys do not change the compositor's modifiers: they
// come from this state of the keymap.
static struct xkb_context *xkb;
static struct xkb_state *xkb_state;

static void fail(const char *what) {
	fprintf(stderr, "droid-nix-desktop-server: %s\n", what);
	exit(1);
}

static void check(int ret, const char *what) {
	if (ret < 0) {
		fprintf(stderr, "droid-nix-desktop-server: %s: %s\n", what, strerror(errno));
		exit(1);
	}
}

static uint32_t now(void) {
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void send_message(int32_t type, int32_t a, int32_t b, int32_t c, int32_t d, int fd) {
	if (client < 0) return;
	int32_t m[5] = { type, a, b, c, d };
	struct iovec iov = { m, sizeof m };
	union {
		char buf[CMSG_SPACE(sizeof(int))];
		struct cmsghdr align;
	} control;
	struct msghdr msg = { .msg_iov = &iov, .msg_iovlen = 1 };
	if (fd >= 0) {
		msg.msg_control = control.buf;
		msg.msg_controllen = sizeof control.buf;
		struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
		cmsg->cmsg_level = SOL_SOCKET;
		cmsg->cmsg_type = SCM_RIGHTS;
		cmsg->cmsg_len = CMSG_LEN(sizeof(int));
		memcpy(CMSG_DATA(cmsg), &fd, sizeof(int));
	}
	// A failed write closes the app's socket, the next read drops it.
	if (sendmsg(client, &msg, MSG_NOSIGNAL) != sizeof m) shutdown(client, SHUT_RDWR);
}

static void frame_buffer(void *data, struct zwlr_screencopy_frame_v1 *f, uint32_t format,
		uint32_t width, uint32_t height, uint32_t stride) {
	if (!buffer || format != fb_format || width != fb_width || height != fb_height || stride != fb_stride) {
		// The app copies these, with or without swapping red and blue.
		if (format != WL_SHM_FORMAT_ARGB8888 && format != WL_SHM_FORMAT_XRGB8888 &&
				format != WL_SHM_FORMAT_ABGR8888 && format != WL_SHM_FORMAT_XBGR8888)
			fail("the compositor's frames are not in a 32 bit RGB format");
		if (buffer) {
			wl_buffer_destroy(buffer);
			close(fb);
		}
		fb = memfd_create("droid-nix-desktop", MFD_CLOEXEC);
		check(fb, "memfd_create");
		check(ftruncate(fb, (off_t)stride * height), "ftruncate");
		struct wl_shm_pool *pool = wl_shm_create_pool(shm, fb, stride * height);
		buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, format);
		wl_shm_pool_destroy(pool);
		fb_format = format;
		fb_width = width;
		fb_height = height;
		fb_stride = stride;
		send_message(0, width, height, stride, format, fb);
		full = true;
	}
	zwlr_screencopy_frame_v1_copy_with_damage(f, buffer);
}

static void frame_flags(void *data, struct zwlr_screencopy_frame_v1 *f, uint32_t flags) {
}

static void frame_damage(void *data, struct zwlr_screencopy_frame_v1 *f,
		uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
	if (damage_x1 >= damage_x2) {
		damage_x1 = x;
		damage_y1 = y;
		damage_x2 = x + width;
		damage_y2 = y + height;
		return;
	}
	if ((int32_t)x < damage_x1) damage_x1 = x;
	if ((int32_t)y < damage_y1) damage_y1 = y;
	if ((int32_t)(x + width) > damage_x2) damage_x2 = x + width;
	if ((int32_t)(y + height) > damage_y2) damage_y2 = y + height;
}

static void frame_ready(void *data, struct zwlr_screencopy_frame_v1 *f,
		uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec) {
	zwlr_screencopy_frame_v1_destroy(f);
	frame = NULL;
	if (full) send_message(1, 0, 0, fb_width, fb_height, -1);
	else send_message(1, damage_x1, damage_y1, damage_x2 - damage_x1, damage_y2 - damage_y1, -1);
	full = false;
}

static void frame_failed(void *data, struct zwlr_screencopy_frame_v1 *f) {
	// E.g. the output changed its size meanwhile: try again once it is done.
	zwlr_screencopy_frame_v1_destroy(f);
	frame = NULL;
	failed = true;
}

static const struct zwlr_screencopy_frame_v1_listener frame_listener = {
	.buffer = frame_buffer,
	.flags = frame_flags,
	.ready = frame_ready,
	.failed = frame_failed,
	.damage = frame_damage,
};

static void capture(void) {
	wanted = false;
	damage_x1 = damage_y1 = damage_x2 = damage_y2 = 0;
	frame = zwlr_screencopy_manager_v1_capture_output(screencopy, 0, output);
	zwlr_screencopy_frame_v1_add_listener(frame, &frame_listener, NULL);
}

static void output_geometry(void *data, struct wl_output *o, int32_t x, int32_t y,
		int32_t physical_width, int32_t physical_height, int32_t subpixel,
		const char *make, const char *model, int32_t transform) {
}

static void output_mode(void *data, struct wl_output *o, uint32_t flags,
		int32_t width, int32_t height, int32_t refresh) {
}

static void output_done(void *data, struct wl_output *o) {
	if (failed) {
		failed = false;
		wanted = true;
	}
}

static void output_scale(void *data, struct wl_output *o, int32_t factor) {
}

static const struct wl_output_listener output_listener = {
	.geometry = output_geometry,
	.mode = output_mode,
	.done = output_done,
	.scale = output_scale,
};

static void pointer_event(bool pressed, int32_t x, int32_t y) {
	if (!buffer) return;
	uint32_t time = now();
	zwlr_virtual_pointer_v1_motion_absolute(pointer, time, x, y, fb_width, fb_height);
	if (pressed != button) {
		zwlr_virtual_pointer_v1_button(pointer, time, BTN_LEFT,
			pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
		button = pressed;
	}
	zwlr_virtual_pointer_v1_frame(pointer);
}

static const char *modifier(xkb_keysym_t keysym) {
	switch (keysym) {
	case XKB_KEY_Shift_L: case XKB_KEY_Shift_R: return "Shift";
	case XKB_KEY_Control_L: case XKB_KEY_Control_R: return "Control";
	case XKB_KEY_Alt_L: case XKB_KEY_Alt_R: return "Mod1";
	case XKB_KEY_Super_L: case XKB_KEY_Super_R: return "Mod4";
	}
	return NULL;
}

// A keymap with a keycode for each keysym the app sent so far.
static void send_keymap(void) {
	char *text;
	size_t size;
	FILE *f = open_memstream(&text, &size);
	fputs("xkb_keymap {\nxkb_keycodes \"droid-nix\" {\nminimum = 8;\nmaximum = 255;\n", f);
	for (int k = 8; k < 8 + keys; k++) fprintf(f, "<K%d> = %d;\n", k, k);
	fputs("};\nxkb_types \"droid-nix\" { include \"complete\" };\n"
		"xkb_compat \"droid-nix\" { include \"complete\" };\n"
		"xkb_symbols \"droid-nix\" {\n", f);
	for (int k = 8; k < 8 + keys; k++) {
		char name[64];
		xkb_keysym_get_name(keysyms[k], name, sizeof name);
		fprintf(f, "key <K%d> { [ %s ] };\n", k, name);
		if (modifier(keysyms[k])) fprintf(f, "modifier_map %s { <K%d> };\n", modifier(keysyms[k]), k);
	}
	fputs("};\n};\n", f);
	if (fclose(f)) fail("cannot write the keymap");
	struct xkb_keymap *keymap = xkb_keymap_new_from_string(xkb, text, XKB_KEYMAP_FORMAT_TEXT_V1, 0);
	if (!keymap) fail("cannot compile the keymap");
	int fd = memfd_create("droid-nix-desktop-keymap", MFD_CLOEXEC);
	check(fd, "memfd_create");
	if (write(fd, text, size + 1) != (ssize_t)(size + 1)) check(-1, "write");
	zwp_virtual_keyboard_v1_keymap(keyboard, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, size + 1);
	close(fd);
	free(text);
	// The keys that are down stay down in the new state.
	xkb_state_unref(xkb_state);
	xkb_state = xkb_state_new(keymap);
	xkb_keymap_unref(keymap);
	for (int k = 8; k < 8 + keys; k++)
		if (pressed[k]) xkb_state_update_key(xkb_state, k, XKB_KEY_DOWN);
}

static void send_modifiers(void) {
	zwp_virtual_keyboard_v1_modifiers(keyboard,
		xkb_state_serialize_mods(xkb_state, XKB_STATE_MODS_DEPRESSED),
		xkb_state_serialize_mods(xkb_state, XKB_STATE_MODS_LATCHED),
		xkb_state_serialize_mods(xkb_state, XKB_STATE_MODS_LOCKED),
		xkb_state_serialize_layout(xkb_state, XKB_STATE_LAYOUT_EFFECTIVE));
}

static void key_event(bool down, xkb_keysym_t keysym) {
	int k = 8;
	while (k < 8 + keys && keysyms[k] != keysym) k++;
	if (k == 8 + keys) {
		if (k == 256) {
			// All keycodes used: start over.
			for (k = 8; k < 256; k++)
				if (pressed[k]) zwp_virtual_keyboard_v1_key(keyboard, now(), k - 8, WL_KEYBOARD_KEY_STATE_RELEASED);
			memset(pressed, 0, sizeof pressed);
			keys = 0;
			k = 8;
		}
		keysyms[k] = keysym;
		keys++;
		send_keymap();
		// Clients start the new keymap without modifiers, but the
		// compositor only passes on changed ones: change them twice.
		zwp_virtual_keyboard_v1_modifiers(keyboard, 0, 0, 0, 0);
		send_modifiers();
	}
	zwp_virtual_keyboard_v1_key(keyboard, now(), k - 8,
		down ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
	pressed[k] = down;
	if (xkb_state_update_key(xkb_state, k, down ? XKB_KEY_DOWN : XKB_KEY_UP)) send_modifiers();
}

static void configured(void *data, struct zwlr_output_configuration_v1 *configuration) {
	zwlr_output_configuration_v1_destroy(configuration);
}

static void resize(void);

static void cancelled(void *data, struct zwlr_output_configuration_v1 *configuration) {
	// The outputs changed meanwhile, the new serial came before this.
	zwlr_output_configuration_v1_destroy(configuration);
	resize();
}

static const struct zwlr_output_configuration_v1_listener configuration_listener = {
	.succeeded = configured,
	.failed = configured,
	.cancelled = cancelled,
};

static void resize(void) {
	if (!head) return;
	struct zwlr_output_configuration_v1 *configuration =
		zwlr_output_manager_v1_create_configuration(output_manager, output_serial);
	zwlr_output_configuration_v1_add_listener(configuration, &configuration_listener, NULL);
	struct zwlr_output_configuration_head_v1 *configuration_head =
		zwlr_output_configuration_v1_enable_head(configuration, head);
	zwlr_output_configuration_head_v1_set_custom_mode(configuration_head, output_width, output_height, 0);
	zwlr_output_configuration_head_v1_destroy(configuration_head);
	zwlr_output_configuration_v1_apply(configuration);
}

static void output_manager_head(void *data, struct zwlr_output_manager_v1 *manager,
		struct zwlr_output_head_v1 *h) {
	// The first one is the first output (wlroots announces them in order).
	if (!head) head = h;
}

static void output_manager_done(void *data, struct zwlr_output_manager_v1 *manager, uint32_t serial) {
	output_serial = serial;
}

static void output_manager_finished(void *data, struct zwlr_output_manager_v1 *manager) {
}

static const struct zwlr_output_manager_v1_listener output_manager_listener = {
	.head = output_manager_head,
	.done = output_manager_done,
	.finished = output_manager_finished,
};

static void drop_client(void) {
	close(client);
	client = -1;
	if (frame) zwlr_screencopy_frame_v1_destroy(frame);
	frame = NULL;
	wanted = failed = false;
	// Nothing stays pressed without the app.
	if (button) pointer_event(false, 0, 0);
	for (int k = 8; k < 256; k++)
		if (pressed[k]) key_event(false, keysyms[k]);
}

static void accept_client(int listener) {
	int c = accept4(listener, NULL, NULL, SOCK_CLOEXEC);
	if (c < 0) return;
	if (client >= 0) drop_client();
	client = c;
	message_size = 0;
	// A new screencopy manager: its first frame has everything as damage.
	if (screencopy) zwlr_screencopy_manager_v1_destroy(screencopy);
	screencopy = wl_registry_bind(registry, screencopy_name, &zwlr_screencopy_manager_v1_interface, 2);
	if (buffer) send_message(0, fb_width, fb_height, fb_stride, fb_format, fb);
}

static void read_client(void) {
	ssize_t n = read(client, (char *)message + message_size, sizeof message - message_size);
	if (n <= 0) {
		drop_client();
		return;
	}
	message_size += n;
	if (message_size < sizeof message) return;
	message_size = 0;
	switch (message[0]) {
	case 0:
		wanted = true;
		break;
	case 1:
		pointer_event(message[1], message[2], message[3]);
		break;
	case 2:
		key_event(message[1], message[2]);
		break;
	case 3:
		output_width = message[1];
		output_height = message[2];
		resize();
		break;
	}
}

static void global(void *data, struct wl_registry *r, uint32_t name, const char *interface, uint32_t version) {
	if (!strcmp(interface, wl_shm_interface.name)) {
		shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
	} else if (!strcmp(interface, wl_seat_interface.name) && !seat) {
		seat = wl_registry_bind(r, name, &wl_seat_interface, 1);
	} else if (!strcmp(interface, wl_output_interface.name) && !output && version >= 2) {
		output = wl_registry_bind(r, name, &wl_output_interface, 2);
		wl_output_add_listener(output, &output_listener, NULL);
	} else if (!strcmp(interface, zwlr_screencopy_manager_v1_interface.name) && version >= 2) {
		screencopy_name = name;
	} else if (!strcmp(interface, zwlr_virtual_pointer_manager_v1_interface.name)) {
		pointer_manager_version = version < 2 ? version : 2;
		pointer_manager = wl_registry_bind(r, name, &zwlr_virtual_pointer_manager_v1_interface,
			pointer_manager_version);
	} else if (!strcmp(interface, zwp_virtual_keyboard_manager_v1_interface.name)) {
		keyboard_manager = wl_registry_bind(r, name, &zwp_virtual_keyboard_manager_v1_interface, 1);
	} else if (!strcmp(interface, zwlr_output_manager_v1_interface.name)) {
		output_manager = wl_registry_bind(r, name, &zwlr_output_manager_v1_interface, 1);
		zwlr_output_manager_v1_add_listener(output_manager, &output_manager_listener, NULL);
	}
}

static void global_remove(void *data, struct wl_registry *r, uint32_t name) {
}

static const struct wl_registry_listener registry_listener = {
	.global = global,
	.global_remove = global_remove,
};

int main(void) {
	const char *path = getenv("DROID_NIX_DESKTOP_SOCKET");
	if (!path) fail("DROID_NIX_DESKTOP_SOCKET is not set");
	display = wl_display_connect(NULL);
	if (!display) fail("cannot connect to the Wayland compositor");
	registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_roundtrip(display);
	if (!shm || !seat || !output) fail("the compositor has no wl_shm, wl_seat or wl_output");
	if (!screencopy_name) fail("the compositor has no wlr-screencopy-unstable-v1 (version 2)");
	if (!pointer_manager) fail("the compositor has no wlr-virtual-pointer-unstable-v1");
	if (!keyboard_manager) fail("the compositor has no virtual-keyboard-unstable-v1");
	pointer = pointer_manager_version >= 2
		? zwlr_virtual_pointer_manager_v1_create_virtual_pointer_with_output(pointer_manager, seat, output)
		: zwlr_virtual_pointer_manager_v1_create_virtual_pointer(pointer_manager, seat);
	keyboard = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(keyboard_manager, seat);
	xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!xkb) fail("cannot create an xkb context");

	struct sockaddr_un address = { .sun_family = AF_UNIX };
	if (strlen(path) >= sizeof address.sun_path) fail("the socket path is too long");
	strcpy(address.sun_path, path);
	int listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	check(listener, "socket");
	unlink(path);
	check(bind(listener, (struct sockaddr *)&address, sizeof address), path);
	check(listen(listener, 1), "listen");

	while (true) {
		while (wl_display_prepare_read(display)) wl_display_dispatch_pending(display);
		wl_display_flush(display);
		struct pollfd fds[] = {
			{ .fd = wl_display_get_fd(display), .events = POLLIN },
			{ .fd = listener, .events = POLLIN },
			{ .fd = client, .events = POLLIN },
		};
		if (poll(fds, 3, -1) < 0) {
			wl_display_cancel_read(display);
			if (errno == EINTR) continue;
			check(-1, "poll");
		}
		if (fds[0].revents) {
			if (wl_display_read_events(display) < 0) fail("lost the compositor");
		} else {
			wl_display_cancel_read(display);
		}
		if (wl_display_dispatch_pending(display) < 0) fail("lost the compositor");
		if (fds[1].revents) accept_client(listener);
		else if (fds[2].revents) read_client();
		if (wanted && !frame && client >= 0) capture();
	}
}
