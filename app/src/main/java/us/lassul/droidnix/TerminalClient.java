package us.lassul.droidnix;

import android.content.ClipData;
import android.content.ClipboardManager;
import android.media.AudioManager;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.util.Log;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;

import androidx.drawerlayout.widget.DrawerLayout;

import com.termux.terminal.KeyHandler;
import com.termux.terminal.TerminalEmulator;
import com.termux.terminal.TerminalSession;
import com.termux.terminal.TerminalSessionClient;
import com.termux.view.TerminalViewClient;

/** The activity's side of the TerminalView and of the sessions while it is bound. */
final class TerminalClient implements TerminalViewClient, TerminalSessionClient {

    private final TerminalActivity activity;
    /** The volume keys, as Ctrl (down) and Fn (up). */
    private boolean volumeDownPressed, volumeUpPressed;

    TerminalClient(TerminalActivity activity) {
        this.activity = activity;
    }

    private boolean isCurrent(TerminalSession session) {
        return session == activity.terminalView.getCurrentSession();
    }

    // TerminalViewClient

    @Override
    public float onScale(float scale) {
        if (scale > 0.9f && scale < 1.1f) return scale;
        activity.changeFontSize(scale > 1);
        return 1;
    }

    @Override
    public void onSingleTapUp(MotionEvent event) {
        TerminalEmulator emulator = activity.terminalView.mEmulator;
        if (emulator != null && !emulator.isMouseTrackingActive() && !event.isFromSource(InputDevice.SOURCE_MOUSE))
            activity.showKeyboard();
    }

    @Override
    public boolean shouldBackButtonBeMappedToEscape() {
        return false;
    }

    @Override
    public boolean shouldEnforceCharBasedInput() {
        return false;
    }

    @Override
    public boolean shouldUseCtrlSpaceWorkaround() {
        return false;
    }

    @Override
    public boolean isTerminalViewSelected() {
        return true;
    }

    @Override
    public void copyModeChanged(boolean copyMode) {
        activity.drawer.setDrawerLockMode(copyMode ? DrawerLayout.LOCK_MODE_LOCKED_CLOSED : DrawerLayout.LOCK_MODE_UNLOCKED);
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event, TerminalSession session) {
        if (handleVolumeKey(keyCode, event, true)) return true;
        if (keyCode == KeyEvent.KEYCODE_ENTER && !session.isRunning()) {
            activity.removeSession(session);
            return true;
        }
        if (!(event.isCtrlPressed() && event.isAltPressed())) return false;

        // Ctrl+Alt shortcuts, with the unmodified character.
        int c = event.getUnicodeChar(0);
        if (keyCode == KeyEvent.KEYCODE_DPAD_DOWN || c == 'n') activity.switchSession(true);
        else if (keyCode == KeyEvent.KEYCODE_DPAD_UP || c == 'p') activity.switchSession(false);
        else if (keyCode == KeyEvent.KEYCODE_DPAD_RIGHT) activity.drawer.openDrawer(Gravity.START);
        else if (keyCode == KeyEvent.KEYCODE_DPAD_LEFT) activity.drawer.closeDrawers();
        else if (c == 'k') activity.toggleKeyboard();
        else if (c == 'm') activity.terminalView.showContextMenu();
        else if (c == 'r') activity.renameSession(session);
        else if (c == 'c') activity.newSession(false);
        else if (c == 'u') activity.selectUrl();
        else if (c == 'v') activity.paste();
        else if (c == '+' || event.getUnicodeChar(KeyEvent.META_SHIFT_ON) == '+') activity.changeFontSize(true);
        else if (c == '-') activity.changeFontSize(false);
        else if (c >= '1' && c <= '9') activity.switchSession(c - '1');
        return true;
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        return handleVolumeKey(keyCode, event, false);
    }

    private boolean handleVolumeKey(int keyCode, KeyEvent event, boolean down) {
        // A full external keyboard keeps its own volume keys.
        InputDevice device = event.getDevice();
        if (device != null && device.getKeyboardType() == InputDevice.KEYBOARD_TYPE_ALPHABETIC) return false;
        if (keyCode == KeyEvent.KEYCODE_VOLUME_DOWN) volumeDownPressed = down;
        else if (keyCode == KeyEvent.KEYCODE_VOLUME_UP) volumeUpPressed = down;
        else return false;
        return true;
    }

    @Override
    public boolean onLongPress(MotionEvent event) {
        return false;
    }

    @Override
    public boolean readControlKey() {
        return volumeDownPressed;
    }

    @Override
    public boolean readAltKey() {
        return false;
    }

    @Override
    public boolean readShiftKey() {
        return false;
    }

    @Override
    public boolean readFnKey() {
        return volumeUpPressed;
    }

    @Override
    public boolean onCodePoint(int codePoint, boolean ctrlDown, TerminalSession session) {
        if (volumeUpPressed) {
            onFnCodePoint(codePoint, session);
            return true;
        }
        // Ctrl+J is Enter.
        if (ctrlDown && codePoint == 'j' && !session.isRunning()) {
            activity.removeSession(session);
            return true;
        }
        return false;
    }

    /** Volume up with a key: arrows, page up/down, function keys and the like. */
    private void onFnCodePoint(int codePoint, TerminalSession session) {
        int keyCode = -1;
        int character = -1;
        boolean alt = false;
        int lowerCase = Character.toLowerCase(codePoint);
        switch (lowerCase) {
            case 'w': keyCode = KeyEvent.KEYCODE_DPAD_UP; break;
            case 'a': keyCode = KeyEvent.KEYCODE_DPAD_LEFT; break;
            case 's': keyCode = KeyEvent.KEYCODE_DPAD_DOWN; break;
            case 'd': keyCode = KeyEvent.KEYCODE_DPAD_RIGHT; break;
            case 'p': keyCode = KeyEvent.KEYCODE_PAGE_UP; break;
            case 'n': keyCode = KeyEvent.KEYCODE_PAGE_DOWN; break;
            case 't': keyCode = KeyEvent.KEYCODE_TAB; break;
            case 'i': keyCode = KeyEvent.KEYCODE_INSERT; break;
            case 'h': character = '~'; break;
            case 'u': character = '_'; break;
            case 'l': character = '|'; break;
            case 'e': character = 27; break; // Escape
            case '.': character = 28; break; // Ctrl+\
            case 'b': case 'f': case 'x': character = lowerCase; alt = true; break; // Alt+b, f, x
            case '0': keyCode = KeyEvent.KEYCODE_F10; break;
            case 'v':
                activity.getSystemService(AudioManager.class).adjustSuggestedStreamVolume(
                    AudioManager.ADJUST_SAME, AudioManager.USE_DEFAULT_STREAM_TYPE, AudioManager.FLAG_SHOW_UI);
                break;
            default:
                if (lowerCase >= '1' && lowerCase <= '9') keyCode = KeyEvent.KEYCODE_F1 + lowerCase - '1';
        }
        TerminalEmulator emulator = session.getEmulator();
        if (keyCode != -1 && emulator != null)
            session.write(KeyHandler.getCode(keyCode, 0, emulator.isCursorKeysApplicationMode(), emulator.isKeypadApplicationMode()));
        else if (character != -1)
            session.writeCodePoint(alt, character);
    }

    @Override
    public void onEmulatorSet() {
        activity.updateBackground();
    }

    // TerminalSessionClient

    @Override
    public void onTextChanged(TerminalSession session) {
        if (isCurrent(session)) activity.terminalView.onScreenUpdated();
    }

    @Override
    public void onTitleChanged(TerminalSession session) {
        activity.updateSessionList();
    }

    @Override
    public void onSessionFinished(TerminalSession session) {
        activity.updateSessionList();
    }

    @Override
    public void onCopyTextToClipboard(TerminalSession session, String text) {
        activity.getSystemService(ClipboardManager.class).setPrimaryClip(ClipData.newPlainText(null, text));
    }

    @Override
    public void onPasteTextFromClipboard(TerminalSession session) {
        ClipData clip = activity.getSystemService(ClipboardManager.class).getPrimaryClip();
        TerminalEmulator emulator = activity.terminalView.mEmulator;
        if (clip == null || emulator == null) return;
        CharSequence text = clip.getItemAt(0).coerceToText(activity);
        if (text != null) emulator.paste(text.toString());
    }

    @Override
    public void onBell(TerminalSession session) {
        activity.getSystemService(Vibrator.class).vibrate(VibrationEffect.createOneShot(50, VibrationEffect.DEFAULT_AMPLITUDE));
    }

    @Override
    public void onColorsChanged(TerminalSession session) {
        if (isCurrent(session)) activity.updateBackground();
    }

    @Override
    public void onTerminalCursorStateChange(boolean state) {
        activity.terminalView.setTerminalCursorBlinkerState(state, false);
    }

    @Override
    public void setTerminalShellPid(TerminalSession session, int pid) {
    }

    @Override
    public Integer getTerminalCursorStyle() {
        return null;
    }

    // Logging, for both

    @Override
    public void logError(String tag, String message) {
        Log.e(tag, message);
    }

    @Override
    public void logWarn(String tag, String message) {
        Log.w(tag, message);
    }

    @Override
    public void logInfo(String tag, String message) {
        Log.i(tag, message);
    }

    @Override
    public void logDebug(String tag, String message) {
        Log.d(tag, message);
    }

    @Override
    public void logVerbose(String tag, String message) {
        Log.v(tag, message);
    }

    @Override
    public void logStackTraceWithMessage(String tag, String message, Exception e) {
        Log.e(tag, message, e);
    }

    @Override
    public void logStackTrace(String tag, Exception e) {
        Log.e(tag, "", e);
    }

}
