package us.lassul.droidnix;

import android.app.Activity;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.os.Bundle;
import android.text.InputType;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.MotionEvent;
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

import java.io.BufferedInputStream;
import java.io.DataInputStream;
import java.io.File;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;

/**
 * The "droid-nix desktop" icon: shows the sway of `droid-nix-desktop`
 * (modules/desktop.nix) fullscreen. wayvnc serves it on files/desktop.sock;
 * this is a small RFB 3.8 client for it: raw frames at the window's size
 * (ExtendedDesktopSize), a finger as the left mouse button, keys as keysyms.
 * A two-finger tap shows or hides the soft keyboard.
 */
public final class DesktopActivity extends Activity implements SurfaceHolder.Callback {

    private static final int EXTENDED_DESKTOP_SIZE = -308;

    private Screen screen;
    private TextView status;
    private LocalSocket socket;
    private OutputStream out;
    // Framebuffer size and its scale on the surface, set by the reader thread.
    private volatile int fbWidth, fbHeight;
    private volatile float scale = 1;
    // wayvnc's screen id, needed to resize it; -1 until it told us.
    private int screenId = -1;
    private int surfaceWidth, surfaceHeight, requestedWidth, requestedHeight;
    private boolean twoFingers;
    // The reader thread's frame, as wayvnc sends it and as a bitmap.
    private byte[] frame;
    private Bitmap bitmap;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        FrameLayout root = new FrameLayout(this);
        screen = new Screen(this);
        screen.getHolder().addCallback(this);
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

    // Reader thread: handshake, then frames until the socket closes.
    private void run(LocalSocket s) {
        try {
            s.connect(new LocalSocketAddress(new File(getFilesDir(), "desktop.sock").getPath(),
                LocalSocketAddress.Namespace.FILESYSTEM));
            DataInputStream in = new DataInputStream(new BufferedInputStream(s.getInputStream(), 1 << 16));
            OutputStream o = s.getOutputStream();
            in.readFully(new byte[12]);
            o.write("RFB 003.008\n".getBytes(StandardCharsets.US_ASCII));
            int types = in.readUnsignedByte();
            if (types == 0) throw new IOException(readString(in));
            in.readFully(new byte[types]);
            o.write(1); // security type None
            if (in.readInt() != 0) throw new IOException(readString(in));
            o.write(1); // ClientInit: shared
            int width = in.readUnsignedShort(), height = in.readUnsignedShort();
            in.readFully(new byte[16]); // the server's pixel format
            readString(in); // desktop name
            allocate(width, height);
            // 32 bit, red/green/blue in the bytes Bitmap.Config.ARGB_8888 has in memory.
            o.write(new byte[] { 0, 0, 0, 0, 32, 24, 0, 1, 0, (byte) 255, 0, (byte) 255, 0, (byte) 255, 0, 8, 16, 0, 0, 0 });
            o.write(ByteBuffer.allocate(12).put((byte) 2).put((byte) 0).putShort((short) 2)
                .putInt(0).putInt(EXTENDED_DESKTOP_SIZE).array()); // raw
            synchronized (this) {
                if (socket != s) return;
                out = o;
            }
            runOnUiThread(() -> status.setVisibility(View.GONE));

            requestUpdate(false, width, height);
            while (true) {
                int type = in.readUnsignedByte();
                if (type == 0) { // FramebufferUpdate
                    in.readUnsignedByte();
                    int rects = in.readUnsignedShort();
                    for (int i = 0; i < rects; i++) {
                        int x = in.readUnsignedShort(), y = in.readUnsignedShort();
                        int w = in.readUnsignedShort(), h = in.readUnsignedShort();
                        int encoding = in.readInt();
                        if (encoding == 0) {
                            for (int row = 0; row < h; row++)
                                in.readFully(frame, ((y + row) * width + x) * 4, w * 4);
                        } else if (encoding == EXTENDED_DESKTOP_SIZE) {
                            // w x h is the (new) framebuffer size.
                            int screens = in.readUnsignedByte();
                            in.readFully(new byte[3]);
                            for (int n = 0; n < screens; n++) {
                                int id = in.readInt();
                                in.readFully(new byte[12]);
                                if (n == 0) synchronized (this) {
                                    screenId = id;
                                }
                            }
                            width = w;
                            height = h;
                            allocate(width, height);
                            resize();
                        } else {
                            throw new IOException("unexpected encoding " + encoding);
                        }
                    }
                    draw();
                    requestUpdate(true, width, height);
                } else if (type == 2) { // Bell
                } else if (type == 3) { // ServerCutText
                    in.readFully(new byte[3]);
                    in.readFully(new byte[in.readInt()]);
                } else {
                    throw new IOException("unexpected message " + type);
                }
            }
        } catch (IOException | RuntimeException e) {
            synchronized (this) {
                if (socket != s) return; // closed in onStop
                out = null;
            }
            runOnUiThread(() -> status.setVisibility(View.VISIBLE));
        }
    }

    private static String readString(DataInputStream in) throws IOException {
        byte[] b = new byte[in.readInt()];
        in.readFully(b);
        return new String(b, StandardCharsets.UTF_8);
    }

    private void allocate(int width, int height) {
        frame = new byte[width * height * 4];
        bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);
        bitmap.setHasAlpha(false);
        fbWidth = width;
        fbHeight = height;
    }

    private void draw() {
        bitmap.copyPixelsFromBuffer(ByteBuffer.wrap(frame));
        SurfaceHolder holder = screen.getHolder();
        Canvas canvas = holder.lockCanvas();
        if (canvas == null) return;
        float s = Math.min((float) canvas.getWidth() / bitmap.getWidth(), (float) canvas.getHeight() / bitmap.getHeight());
        scale = s;
        canvas.drawColor(Color.BLACK);
        canvas.scale(s, s);
        canvas.drawBitmap(bitmap, 0, 0, new Paint(Paint.FILTER_BITMAP_FLAG));
        holder.unlockCanvasAndPost(canvas);
    }

    private synchronized void send(byte[] message) {
        if (out == null) return;
        try {
            out.write(message);
        } catch (IOException ignored) {
            // The reader sees the closed socket.
        }
    }

    private void requestUpdate(boolean incremental, int width, int height) {
        send(ByteBuffer.allocate(10).put((byte) 3).put((byte) (incremental ? 1 : 0))
            .putShort((short) 0).putShort((short) 0).putShort((short) width).putShort((short) height).array());
    }

    // Asks wayvnc to make sway's output the size of the surface (SetDesktopSize).
    private synchronized void resize() {
        int w = surfaceWidth, h = surfaceHeight;
        if (screenId < 0 || w == 0 || (w == requestedWidth && h == requestedHeight)) return;
        requestedWidth = w;
        requestedHeight = h;
        send(ByteBuffer.allocate(24).put((byte) 251).put((byte) 0).putShort((short) w).putShort((short) h)
            .put((byte) 1).put((byte) 0)
            .putInt(screenId).putShort((short) 0).putShort((short) 0).putShort((short) w).putShort((short) h).putInt(0)
            .array());
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        synchronized (this) {
            surfaceWidth = width;
            surfaceHeight = height;
        }
        resize();
        requestUpdate(false, fbWidth, fbHeight); // the new surface is empty
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
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

    private void pointer(int buttons, MotionEvent e) {
        int x = Math.max(0, Math.min(fbWidth - 1, (int) (e.getX(0) / scale)));
        int y = Math.max(0, Math.min(fbHeight - 1, (int) (e.getY(0) / scale)));
        send(ByteBuffer.allocate(6).put((byte) 5).put((byte) buttons).putShort((short) x).putShort((short) y).array());
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
        send(ByteBuffer.allocate(8).put((byte) 4).put((byte) (down ? 1 : 0)).putShort((short) 0).putInt(keysym).array());
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
