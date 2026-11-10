package us.lassul.droidnix;

import android.app.Activity;
import android.content.Context;
import android.graphics.PixelFormat;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;
import android.system.OsConstants;
import android.text.InputType;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.widget.FrameLayout;
import android.widget.TextView;

import androidx.core.graphics.Insets;
import androidx.core.view.ViewCompat;
import androidx.core.view.WindowCompat;
import androidx.core.view.WindowInsetsCompat;
import androidx.core.view.WindowInsetsControllerCompat;

import java.io.EOFException;
import java.io.File;
import java.io.FileDescriptor;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/**
 * The "droid-nix desktop" icon: shows the sway of `droid-nix-desktop`
 * (modules/desktop.nix) fullscreen. droid-nix-desktop-server
 * (pkgs/droid-nix-desktop-server/server.c, which describes the messages)
 * serves it on files/desktop.sock: it shares two frame buffers with this
 * activity and says which one has the next frame and which part of it
 * changed; the native draw copies that part into the window while the next
 * frame can go into the other buffer. A finger is the left mouse button,
 * keys go as keysyms. A two-finger tap shows or hides the soft keyboard.
 */
public final class DesktopActivity extends Activity implements SurfaceHolder.Callback {

    static {
        System.loadLibrary("desktop");
    }

    // app/jni/desktop.c
    private static native long acquireWindow(Surface surface);
    private static native void releaseWindow(long window);
    private static native void draw(long window, long frame, int width, int height, int stride, boolean swap,
        int x, int y, int w, int h);

    private FrameLayout root;
    private Screen screen;
    private TextView status;
    // Guarded by this: the connection, the window, the space for the desktop
    // (the output's size), the mapped frame buffers and the frame last shown
    // (its address, 0 if none). Sending only needs out, so that touch and
    // keys never wait for a frame being drawn.
    private LocalSocket socket;
    private volatile OutputStream out;
    private long window;
    private int availableWidth, availableHeight;
    private long frame, frameSize, shown;
    private volatile int fbWidth, fbHeight;
    private int fbStride;
    private boolean swap;
    private boolean twoFingers;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        root = new FrameLayout(this);
        screen = new Screen(this);
        screen.getHolder().addCallback(this);
        screen.getHolder().setFormat(PixelFormat.RGBX_8888);
        screen.setOnTouchListener((v, e) -> touch(e));
        root.addView(screen);
        status = new TextView(this);
        status.setGravity(Gravity.CENTER);
        status.setText(R.string.desktop_not_running);
        status.setVisibility(View.GONE);
        root.addView(status);
        setContentView(root);
        screen.requestFocus();

        WindowInsetsControllerCompat bars = WindowCompat.getInsetsController(getWindow(), root);
        bars.hide(WindowInsetsCompat.Type.systemBars());
        bars.setSystemBarsBehavior(WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
        // The soft keyboard shrinks the desktop instead of covering it.
        ViewCompat.setOnApplyWindowInsetsListener(root, (view, insets) -> {
            Insets ime = insets.getInsets(WindowInsetsCompat.Type.ime());
            view.setPadding(0, 0, 0, ime.bottom);
            return WindowInsetsCompat.CONSUMED;
        });
        // The output gets the size of the space for it.
        root.addOnLayoutChangeListener((v, l, t, r, b, oldL, oldT, oldR, oldB) -> {
            int w = v.getWidth() - v.getPaddingLeft() - v.getPaddingRight();
            int h = v.getHeight() - v.getPaddingTop() - v.getPaddingBottom();
            synchronized (this) {
                if (w == availableWidth && h == availableHeight) return;
                availableWidth = w;
                availableHeight = h;
            }
            resize();
            root.post(this::layoutScreen);
        });
    }

    @Override
    protected void onStart() {
        super.onStart();
        LocalSocket s = new LocalSocket();
        synchronized (this) {
            socket = s;
        }
        new Thread(() -> run(s), "desktop").start();
    }

    @Override
    protected void onStop() {
        super.onStop();
        synchronized (this) {
            try {
                socket.close();
            } catch (IOException ignored) {
            }
            socket = null;
            out = null;
        }
    }

    // Reader thread: the server's messages until the socket closes.
    private void run(LocalSocket s) {
        try {
            s.connect(new LocalSocketAddress(new File(getFilesDir(), "desktop.sock").getPath(),
                LocalSocketAddress.Namespace.FILESYSTEM));
            InputStream in = s.getInputStream();
            OutputStream o = s.getOutputStream();
            synchronized (this) {
                if (socket != s) return;
                out = o;
            }
            runOnUiThread(() -> status.setVisibility(View.GONE));
            resize();
            byte[] b = new byte[24];
            ByteBuffer m = ByteBuffer.wrap(b).order(ByteOrder.nativeOrder());
            while (true) {
                for (int n = 0; n < b.length; ) {
                    int r = in.read(b, n, b.length - n);
                    if (r < 0) throw new EOFException();
                    n += r;
                }
                if (m.getInt(0) == 0) { // new frame buffers
                    FileDescriptor[] fds = s.getAncillaryFileDescriptors();
                    if (fds == null || fds.length != 1) throw new IOException("no frame buffer");
                    map(s, fds[0], m.getInt(4), m.getInt(8), m.getInt(12), m.getInt(16));
                    runOnUiThread(this::layoutScreen);
                } else { // a frame
                    synchronized (this) {
                        if (socket != s) return;
                        shown = frame + m.getInt(4) * (long) fbStride * fbHeight;
                        if (window != 0)
                            draw(window, shown, fbWidth, fbHeight, fbStride, swap, m.getInt(8), m.getInt(12), m.getInt(16), m.getInt(20));
                    }
                    send(0, 0, 0, 0); // done with it
                }
            }
        } catch (IOException | ErrnoException | RuntimeException e) {
            synchronized (this) {
                if (socket != s) return; // closed in onStop
                out = null;
            }
            runOnUiThread(() -> status.setVisibility(View.VISIBLE));
        }
    }

    // Maps the server's frame buffers instead of the previous ones.
    private synchronized void map(LocalSocket s, FileDescriptor fd, int width, int height, int stride, int format)
            throws ErrnoException {
        try {
            if (socket != s) return;
            if (frame != 0) Os.munmap(frame, frameSize);
            frame = shown = 0;
            frameSize = 2L * stride * height;
            frame = Os.mmap(0, frameSize, OsConstants.PROT_READ, OsConstants.MAP_SHARED, fd, 0);
            fbWidth = width;
            fbHeight = height;
            fbStride = stride;
            // wl_shm's ARGB8888 and XRGB8888 are B, G, R, X in memory.
            swap = format == 0 || format == 1;
        } finally {
            Os.close(fd);
        }
    }

    // Scales the screen to the space for it, keeping the frame's aspect ratio.
    private void layoutScreen() {
        int w, h, aw, ah;
        synchronized (this) {
            w = fbWidth;
            h = fbHeight;
            aw = availableWidth;
            ah = availableHeight;
        }
        if (w == 0 || aw == 0) return;
        float s = Math.min((float) aw / w, (float) ah / h);
        FrameLayout.LayoutParams p = (FrameLayout.LayoutParams) screen.getLayoutParams();
        int pw = Math.round(w * s), ph = Math.round(h * s);
        if (p.width == pw && p.height == ph) return;
        p.width = pw;
        p.height = ph;
        screen.setLayoutParams(p);
    }

    private void send(int type, int a, int b, int c) {
        OutputStream o = out;
        if (o == null) return;
        try {
            synchronized (o) {
                o.write(ByteBuffer.allocate(16).order(ByteOrder.nativeOrder())
                    .putInt(type).putInt(a).putInt(b).putInt(c).array());
            }
        } catch (IOException ignored) {
            // The reader sees the closed socket.
        }
    }

    // Asks the server to make the output the size of the space for it.
    private synchronized void resize() {
        if (availableWidth > 0 && availableHeight > 0) send(3, availableWidth, availableHeight, 0);
    }

    @Override
    public synchronized void surfaceCreated(SurfaceHolder holder) {
        window = acquireWindow(holder.getSurface());
    }

    @Override
    public synchronized void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        // The new surface is empty. The server may be capturing a newer frame
        // into the shown one meanwhile; the frames that follow redraw what
        // differs.
        if (window != 0 && shown != 0) draw(window, shown, fbWidth, fbHeight, fbStride, swap, 0, 0, fbWidth, fbHeight);
    }

    @Override
    public synchronized void surfaceDestroyed(SurfaceHolder holder) {
        releaseWindow(window);
        window = 0;
    }

    private boolean touch(MotionEvent e) {
        switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                twoFingers = false;
                pointer(1, e);
                break;
            case MotionEvent.ACTION_MOVE:
                if (!twoFingers) pointer(1, e);
                break;
            case MotionEvent.ACTION_POINTER_DOWN:
                if (!twoFingers) {
                    twoFingers = true;
                    pointer(0, e);
                    toggleKeyboard();
                }
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                if (!twoFingers) pointer(0, e);
                break;
        }
        return true;
    }

    // buttons: 1 if the left one is pressed.
    private void pointer(int buttons, MotionEvent e) {
        int w = fbWidth, h = fbHeight;
        if (w == 0) return;
        int x = Math.max(0, Math.min(w - 1, (int) (e.getX(0) * w / screen.getWidth())));
        int y = Math.max(0, Math.min(h - 1, (int) (e.getY(0) * h / screen.getHeight())));
        send(1, buttons, x, y);
    }

    private void toggleKeyboard() {
        WindowInsetsCompat insets = ViewCompat.getRootWindowInsets(screen);
        WindowInsetsControllerCompat controller = WindowCompat.getInsetsController(getWindow(), screen);
        if (insets != null && insets.isVisible(WindowInsetsCompat.Type.ime()))
            controller.hide(WindowInsetsCompat.Type.ime());
        else
            controller.show(WindowInsetsCompat.Type.ime());
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent e) {
        if (e.getAction() == KeyEvent.ACTION_MULTIPLE && e.getKeyCode() == KeyEvent.KEYCODE_UNKNOWN) {
            e.getCharacters().codePoints().forEach(c -> {
                key(keysym(c), true);
                key(keysym(c), false);
            });
            return true;
        }
        int sym = keysym(e);
        if (sym == 0) return super.dispatchKeyEvent(e);
        key(sym, e.getAction() == KeyEvent.ACTION_DOWN);
        return true;
    }

    private void key(int keysym, boolean down) {
        send(2, down ? 1 : 0, keysym, 0);
    }

    private static int keysym(int codePoint) {
        return codePoint < 0x100 ? codePoint : 0x01000000 | codePoint;
    }

    private static int keysym(KeyEvent e) {
        switch (e.getKeyCode()) {
            case KeyEvent.KEYCODE_ENTER: case KeyEvent.KEYCODE_NUMPAD_ENTER: return 0xff0d;
            case KeyEvent.KEYCODE_DEL: return 0xff08;
            case KeyEvent.KEYCODE_FORWARD_DEL: return 0xffff;
            case KeyEvent.KEYCODE_TAB: return 0xff09;
            case KeyEvent.KEYCODE_ESCAPE: return 0xff1b;
            case KeyEvent.KEYCODE_DPAD_LEFT: return 0xff51;
            case KeyEvent.KEYCODE_DPAD_UP: return 0xff52;
            case KeyEvent.KEYCODE_DPAD_RIGHT: return 0xff53;
            case KeyEvent.KEYCODE_DPAD_DOWN: return 0xff54;
            case KeyEvent.KEYCODE_PAGE_UP: return 0xff55;
            case KeyEvent.KEYCODE_PAGE_DOWN: return 0xff56;
            case KeyEvent.KEYCODE_MOVE_HOME: return 0xff50;
            case KeyEvent.KEYCODE_MOVE_END: return 0xff57;
            case KeyEvent.KEYCODE_INSERT: return 0xff63;
            case KeyEvent.KEYCODE_SHIFT_LEFT: return 0xffe1;
            case KeyEvent.KEYCODE_SHIFT_RIGHT: return 0xffe2;
            case KeyEvent.KEYCODE_CTRL_LEFT: return 0xffe3;
            case KeyEvent.KEYCODE_CTRL_RIGHT: return 0xffe4;
            case KeyEvent.KEYCODE_ALT_LEFT: return 0xffe9;
            case KeyEvent.KEYCODE_ALT_RIGHT: return 0xffea;
            case KeyEvent.KEYCODE_META_LEFT: return 0xffeb;
            case KeyEvent.KEYCODE_META_RIGHT: return 0xffec;
        }
        if (e.getKeyCode() >= KeyEvent.KEYCODE_F1 && e.getKeyCode() <= KeyEvent.KEYCODE_F12)
            return 0xffbe + e.getKeyCode() - KeyEvent.KEYCODE_F1;
        // The character without Ctrl/Alt/Meta: those go as their own keys.
        int c = e.getUnicodeChar(e.getMetaState()
            & ~(KeyEvent.META_CTRL_MASK | KeyEvent.META_ALT_MASK | KeyEvent.META_META_MASK));
        return c <= 0 ? 0 : keysym(c); // < 0: a dead key
    }

    /** The surface, as a text editor so that the soft keyboard sends its keys here. */
    private static final class Screen extends SurfaceView {

        Screen(Context context) {
            super(context);
            setFocusable(true);
            setFocusableInTouchMode(true);
        }

        @Override
        public boolean onCheckIsTextEditor() {
            return true;
        }

        @Override
        public InputConnection onCreateInputConnection(EditorInfo outAttrs) {
            // No text field: the keyboard sends key events.
            outAttrs.inputType = InputType.TYPE_NULL;
            return new BaseInputConnection(this, false);
        }

    }

}
