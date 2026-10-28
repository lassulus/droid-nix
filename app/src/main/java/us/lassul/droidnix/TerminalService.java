package us.lassul.droidnix;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.graphics.drawable.Icon;
import android.os.IBinder;
import android.util.Log;

import com.termux.terminal.TerminalSession;
import com.termux.terminal.TerminalSessionClient;

import java.util.ArrayList;
import java.util.List;

/**
 * Keeps the terminal sessions running while the activity is gone, as a
 * foreground service with a notification.
 */
public final class TerminalService extends Service {

    static final String ACTION_STOP = "us.lassul.droidnix.service_stop";

    private static final int NOTIFICATION_ID = 1;
    private static final String CHANNEL = "sessions";

    final List<TerminalSession> sessions = new ArrayList<>();
    /** The session the activity shows. */
    TerminalSession current;
    /** The activity while it is bound: its client gets the sessions' events. */
    private TerminalActivity activity;

    final class Binder extends android.os.Binder {
        TerminalService service() {
            return TerminalService.this;
        }
    }

    @Override
    public void onCreate() {
        getSystemService(NotificationManager.class).createNotificationChannel(
            new NotificationChannel(CHANNEL, getString(R.string.notification_channel), NotificationManager.IMPORTANCE_LOW));
        startForeground(NOTIFICATION_ID, notification());
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null && ACTION_STOP.equals(intent.getAction())) {
            if (activity != null) activity.finish();
            for (TerminalSession session : sessions) session.finishIfRunning();
            sessions.clear();
            stopSelf();
        }
        return START_NOT_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return new Binder();
    }

    @Override
    public void onDestroy() {
        for (TerminalSession session : sessions) session.finishIfRunning();
    }

    void setActivity(TerminalActivity activity) {
        this.activity = activity;
        for (TerminalSession session : sessions) session.updateTerminalSessionClient(client());
    }

    private TerminalSessionClient client() {
        return activity != null ? activity.client : DETACHED;
    }

    /** A new session with the login, or the failsafe shell: Android's /system/bin/sh. */
    TerminalSession addSession(boolean failsafe) {
        String[] command = failsafe ? new String[]{"/system/bin/sh"} : Shell.login(this);
        TerminalSession session = new TerminalSession(command[0], Shell.home(this).getPath(), command,
            Shell.environment(this, failsafe), null, client());
        if (failsafe) session.mSessionName = getString(R.string.failsafe);
        sessions.add(session);
        updateNotification();
        return session;
    }

    void removeSession(TerminalSession session) {
        session.finishIfRunning();
        sessions.remove(session);
        if (sessions.isEmpty()) stopSelf();
        else updateNotification();
    }

    void updateNotification() {
        getSystemService(NotificationManager.class).notify(NOTIFICATION_ID, notification());
    }

    private Notification notification() {
        int count = sessions.size();
        Intent stop = new Intent(this, TerminalService.class).setAction(ACTION_STOP);
        return new Notification.Builder(this, CHANNEL)
            .setSmallIcon(R.drawable.ic_foreground_nix)
            .setContentTitle(getString(R.string.app_name))
            .setContentText(getResources().getQuantityString(R.plurals.sessions, count, count))
            .setContentIntent(PendingIntent.getActivity(this, 0,
                new Intent(this, TerminalActivity.class), PendingIntent.FLAG_IMMUTABLE))
            .setOngoing(true)
            .setShowWhen(false)
            .addAction(new Notification.Action.Builder(
                Icon.createWithResource(this, android.R.drawable.ic_delete), getString(R.string.exit),
                PendingIntent.getService(this, 0, stop, PendingIntent.FLAG_IMMUTABLE)).build())
            .build();
    }

    /** The sessions' client while no activity is bound. */
    private static final TerminalSessionClient DETACHED = new TerminalSessionClient() {
        @Override public void onTextChanged(TerminalSession session) { }
        @Override public void onTitleChanged(TerminalSession session) { }
        @Override public void onSessionFinished(TerminalSession session) { }
        @Override public void onCopyTextToClipboard(TerminalSession session, String text) { }
        @Override public void onPasteTextFromClipboard(TerminalSession session) { }
        @Override public void onBell(TerminalSession session) { }
        @Override public void onColorsChanged(TerminalSession session) { }
        @Override public void onTerminalCursorStateChange(boolean state) { }
        @Override public void setTerminalShellPid(TerminalSession session, int pid) { }
        @Override public Integer getTerminalCursorStyle() { return null; }
        @Override public void logError(String tag, String message) { Log.e(tag, message); }
        @Override public void logWarn(String tag, String message) { Log.w(tag, message); }
        @Override public void logInfo(String tag, String message) { }
        @Override public void logDebug(String tag, String message) { }
        @Override public void logVerbose(String tag, String message) { }
        @Override public void logStackTraceWithMessage(String tag, String message, Exception e) { Log.e(tag, message, e); }
        @Override public void logStackTrace(String tag, Exception e) { Log.e(tag, "", e); }
    };

}
