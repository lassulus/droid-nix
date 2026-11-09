// DesktopActivity's native part: copies a region of the desktop's frame
// buffer (shared with droid-nix-desktop-server) into the SurfaceView's
// window, whose buffers have the frame's size (the system scales them).
#include <android/native_window_jni.h>
#include <jni.h>
#include <stdint.h>
#include <string.h>

JNIEXPORT jlong JNICALL
Java_us_lassul_droidnix_DesktopActivity_acquireWindow(JNIEnv *env, jclass cls, jobject surface) {
	return (jlong)(intptr_t)ANativeWindow_fromSurface(env, surface);
}

JNIEXPORT void JNICALL
Java_us_lassul_droidnix_DesktopActivity_releaseWindow(JNIEnv *env, jclass cls, jlong window) {
	ANativeWindow_release((ANativeWindow *)(intptr_t)window);
}

// frame: width x height pixels, stride bytes per row, 32 bit; swap: its
// bytes are B, G, R, X (wl_shm's [AX]RGB8888) instead of R, G, B, X.
JNIEXPORT void JNICALL
Java_us_lassul_droidnix_DesktopActivity_draw(JNIEnv *env, jclass cls, jlong window, jlong frame,
		jint width, jint height, jint stride, jboolean swap, jint x, jint y, jint w, jint h) {
	ANativeWindow *win = (ANativeWindow *)(intptr_t)window;
	if (ANativeWindow_setBuffersGeometry(win, width, height, WINDOW_FORMAT_RGBX_8888)) return;
	ANativeWindow_Buffer buffer;
	// The window may want more redrawn than asked for, e.g. all of a new buffer.
	ARect dirty = { x, y, x + w, y + h };
	if (ANativeWindow_lock(win, &buffer, &dirty)) return;
	int right = dirty.right < buffer.width ? dirty.right : buffer.width;
	int bottom = dirty.bottom < buffer.height ? dirty.bottom : buffer.height;
	if (right > width) right = width;
	if (bottom > height) bottom = height;
	int n = right - dirty.left;
	for (int row = dirty.top; n > 0 && row < bottom; row++) {
		const uint32_t *src = (const uint32_t *)((const char *)(intptr_t)frame + (size_t)row * stride) + dirty.left;
		uint32_t *dst = (uint32_t *)buffer.bits + (size_t)row * buffer.stride + dirty.left;
		if (!swap) {
			memcpy(dst, src, n * 4);
			continue;
		}
		for (int i = 0; i < n; i++) {
			uint32_t p = src[i];
			dst[i] = (p & 0xff00ff00) | (p >> 16 & 0xff) | (p & 0xff) << 16;
		}
	}
	ANativeWindow_unlockAndPost(win);
}
