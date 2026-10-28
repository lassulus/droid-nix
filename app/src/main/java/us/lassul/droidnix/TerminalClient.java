package us.lassul.droidnix;

import android.content.ClipData;
import android.content.ClipboardManager;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.util.Log;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;

import com.termux.terminal.TerminalEmulator;
import com.termux.terminal.TerminalSession;
import com.termux.terminal.TerminalSessionClient;
import com.termux.view.TerminalViewClient;

/** The activity's side of the TerminalView and of the sessions while it is bound. */
final class TerminalClient implements TerminalViewClient, TerminalSessionClient {

    private final TerminalActivity activity;

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
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event, TerminalSession session) {
        if (keyCode == KeyEvent.KEYCODE_ENTER && !session.isRunning()) {
            activity.removeSession(session);
            return true;
        }
        return false;
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        return false;
    }

    @Override
    public boolean onLongPress(MotionEvent event) {
        return false;
    }

    @Override
    public boolean readControlKey() {
        return false;
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
        return false;
    }

    @Override
    public boolean onCodePoint(int codePoint, boolean ctrlDown, TerminalSession session) {
        // Ctrl+J is Enter.
        if (ctrlDown && codePoint == 'j' && !session.isRunning()) {
            activity.removeSession(session);
            return true;
        }
        return false;
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
    }

    @Override
    public void onSessionFinished(TerminalSession session) {
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
