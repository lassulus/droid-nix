package us.lassul.droidnix;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.graphics.drawable.Icon;
import android.net.Uri;
import android.net.wifi.WifiManager;
import android.os.Handler;
import android.os.IBinder;
import android.os.PowerManager;
import android.provider.Settings;
import android.util.Log;

import com.termux.terminal.TerminalSession;
import com.termux.terminal.TerminalSessionClient;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/**
 * Keeps the terminal sessions running while the activity is gone, as a
 * foreground service with a notification.
 */
public final class TerminalService extends Service {

    static final String ACTION_STOP = "us.lassul.droidnix.service_stop";
    /** termux-wake-lock and termux-wake-unlock, and the notification's button. */
    static final String ACTION_WAKE_LOCK = "us.lassul.droidnix.service_wake_lock";
    static final String ACTION_WAKE_UNLOCK = "us.lassul.droidnix.service_wake_unlock";
    /** Termux's service_execute (com.termux.shared.termux.TermuxConstants in termux-shared-compat). */
    static final String ACTION_EXECUTE = "us.lassul.droidnix.service_execute";
    static final String EXTRA_ARGUMENTS = "us.lassul.droidnix.execute.arguments";
    static final String EXTRA_WORKDIR = "us.lassul.droidnix.execute.cwd";
    static final String EXTRA_STDIN = "us.lassul.droidnix.execute.stdin";
    static final String EXTRA_PENDING_INTENT = "pendingIntent";

    private static final int NOTIFICATION_ID = 1;
    private static final String CHANNEL = "sessions";

    final List<TerminalSession> sessions = new ArrayList<>();
    /** The session the activity shows. */
    TerminalSession current;
    /** The activity while it is bound: its client gets the sessions' events. */
    private TerminalActivity activity;
    private PowerManager.WakeLock wakeLock;
    private WifiManager.WifiLock wifiLock;
    private int backgroundCommands;

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
        String action = intent != null ? intent.getAction() : null;
        if (ACTION_STOP.equals(action)) {
            if (activity != null) activity.finish();
            for (TerminalSession session : sessions) session.finishIfRunning();
            sessions.clear();
            stopSelf();
        } else if (ACTION_WAKE_LOCK.equals(action)) {
            acquireWakeLock();
        } else if (ACTION_WAKE_UNLOCK.equals(action)) {
            releaseWakeLock();
        } else if (ACTION_EXECUTE.equals(action)) {
            execute(intent);
        }
        return START_NOT_STICKY;
    }

    /**
     * Runs the executable in the intent's data (scheme us.lassul.droidnix.file)
     * in the background: Termux:API's notification actions and jobs.
     */
    private void execute(Intent intent) {
        String[] arguments = intent.getStringArrayExtra(EXTRA_ARGUMENTS);
        String[] command = Shell.command(intent.getData().getPath(), arguments != null ? arguments : new String[0]);
        backgroundCommands++;
        BackgroundCommand.start(this, command, intent.getStringExtra(EXTRA_WORKDIR), intent.getStringExtra(EXTRA_STDIN),
            (PendingIntent) intent.getParcelableExtra(EXTRA_PENDING_INTENT), () -> new Handler(getMainLooper()).post(() -> {
                backgroundCommands--;
                stopIfIdle();
            }));
    }

    /** Stops the service when nothing runs in it and no activity shows it. */
    private void stopIfIdle() {
        if (sessions.isEmpty() && backgroundCommands == 0 && activity == null) stopSelf();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return new Binder();
    }

    @Override
    public void onDestroy() {
        releaseWakeLock();
        for (TerminalSession session : sessions) session.finishIfRunning();
    }

    /**
     * Keeps the CPU and Wi-Fi running with the screen off. Android still stops
     * the app in the background unless it is exempt from battery
     * optimization, so it asks for that.
     */
    private void acquireWakeLock() {
        if (wakeLock != null) return;
        wakeLock = getSystemService(PowerManager.class).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "droid-nix:wake-lock");
        wakeLock.acquire();
        wifiLock = getSystemService(WifiManager.class).createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF, "droid-nix:wake-lock");
        wifiLock.acquire();
        if (!getSystemService(PowerManager.class).isIgnoringBatteryOptimizations(getPackageName())) {
            try {
                startActivity(new Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS,
                    Uri.parse("package:" + getPackageName())).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
            } catch (RuntimeException e) {
                Log.w("droid-nix", "cannot ask to ignore battery optimizations", e);
            }
        }
        updateNotification();
    }

    private void releaseWakeLock() {
        if (wakeLock == null) return;
        wakeLock.release();
        wakeLock = null;
        wifiLock.release();
        wifiLock = null;
        updateNotification();
    }

    void setActivity(TerminalActivity activity) {
        this.activity = activity;
        for (TerminalSession session : sessions) session.updateTerminalSessionClient(client());
        stopIfIdle();
    }

    private TerminalSessionClient client() {
        return activity != null ? activity.client : DETACHED;
    }

    /** A new session with the login, or the failsafe shell: Android's /system/bin/sh. */
    TerminalSession addSession(boolean failsafe) {
        String[] command = failsafe ? new String[]{"/system/bin/sh"} : Shell.login(this);
        Integer transcriptRows;
        try {
            transcriptRows = new Props(this).getInteger("terminal-transcript-rows");
        } catch (IOException e) {
            transcriptRows = null;
        }
        TerminalSession session = new TerminalSession(command[0], Shell.home(this).getPath(), command,
            Shell.environment(this, failsafe), transcriptRows, client());
        if (failsafe) session.mSessionName = getString(R.string.failsafe);
        sessions.add(session);
        updateNotification();
        return session;
    }

    void removeSession(TerminalSession session) {
        session.finishIfRunning();
        sessions.remove(session);
        updateNotification();
        stopIfIdle();
    }

    void updateNotification() {
        getSystemService(NotificationManager.class).notify(NOTIFICATION_ID, notification());
    }

    private Notification notification() {
        int count = sessions.size();
        String text = getResources().getQuantityString(R.plurals.sessions, count, count);
        if (wakeLock != null) text += getString(R.string.wake_lock_held);
        Intent stop = new Intent(this, TerminalService.class).setAction(ACTION_STOP);
        Intent wake = new Intent(this, TerminalService.class).setAction(wakeLock != null ? ACTION_WAKE_UNLOCK : ACTION_WAKE_LOCK);
        return new Notification.Builder(this, CHANNEL)
            .setSmallIcon(R.drawable.ic_foreground_nix)
            .setContentTitle(getString(R.string.app_name))
            .setContentText(text)
            .setContentIntent(PendingIntent.getActivity(this, 0,
                new Intent(this, TerminalActivity.class), PendingIntent.FLAG_IMMUTABLE))
            .setOngoing(true)
            .setShowWhen(false)
            .addAction(new Notification.Action.Builder(
                Icon.createWithResource(this, android.R.drawable.ic_delete), getString(R.string.exit),
                PendingIntent.getService(this, 0, stop, PendingIntent.FLAG_IMMUTABLE)).build())
            .addAction(new Notification.Action.Builder(
                Icon.createWithResource(this, android.R.drawable.ic_lock_lock),
                getString(wakeLock != null ? R.string.release_wake_lock : R.string.acquire_wake_lock),
                PendingIntent.getService(this, 1, wake, PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT)).build())
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
