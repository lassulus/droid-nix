package us.lassul.droidnix;

import android.app.Notification;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.IBinder;
import android.util.Log;

import java.io.IOException;

/**
 * Termux's RUN_COMMAND intent for other apps (Tasker, ...), with
 * us.lassul.droidnix in place of com.termux: action
 * us.lassul.droidnix.RUN_COMMAND, extras us.lassul.droidnix.RUN_COMMAND_*,
 * guarded by the permission us.lassul.droidnix.permission.RUN_COMMAND and
 * allow-external-apps in droid-nix.properties. The command goes to
 * TerminalService's execute action; result files (RUN_COMMAND_RESULT_*)
 * are not supported, the PendingIntent is.
 */
public final class RunCommandService extends Service {

    private static final String PREFIX = "us.lassul.droidnix.RUN_COMMAND_";
    private static final int NOTIFICATION_ID = 2;

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        // The caller may have used startForegroundService.
        startForeground(NOTIFICATION_ID, new Notification.Builder(this, TerminalService.CHANNEL)
            .setSmallIcon(R.drawable.ic_foreground_nix)
            .setContentTitle(getString(R.string.run_command))
            .build());
        try {
            run(intent);
        } finally {
            stopSelf(startId);
        }
        return START_NOT_STICKY;
    }

    private void run(Intent intent) {
        PendingIntent pendingIntent = intent.getParcelableExtra(PREFIX + "PENDING_INTENT");
        boolean allowed;
        try {
            allowed = new Props(this).getBoolean("allow-external-apps", true);
        } catch (IOException e) {
            allowed = false;
        }
        String path = intent.getStringExtra(PREFIX + "PATH");
        if (!allowed || path == null) {
            String error = !allowed ? getString(R.string.run_command_not_allowed) : "missing " + PREFIX + "PATH";
            Log.w("droid-nix", "RUN_COMMAND: " + error);
            sendError(pendingIntent, error);
            return;
        }

        String[] arguments = intent.getStringArrayExtra(PREFIX + "ARGUMENTS");
        // A Tasker array can not have commas in its items, so callers may use another character.
        if (arguments != null && intent.getBooleanExtra(PREFIX + "REPLACE_COMMA_ALTERNATIVE_CHARS_IN_ARGUMENTS", false)) {
            String alternative = intent.getStringExtra(PREFIX + "COMMA_ALTERNATIVE_CHARS_IN_ARGUMENTS");
            if (alternative == null) alternative = "‚";
            for (int i = 0; i < arguments.length; i++)
                for (int c = 0; c < alternative.length(); c++)
                    arguments[i] = arguments[i].replace(alternative.charAt(c), ',');
        }

        Intent execute = new Intent(TerminalService.ACTION_EXECUTE, new Uri.Builder().scheme("us.lassul.droidnix.file").path(path).build())
            .setClass(this, TerminalService.class)
            .putExtra(TerminalService.EXTRA_ARGUMENTS, arguments)
            .putExtra(TerminalService.EXTRA_STDIN, intent.getStringExtra(PREFIX + "STDIN"))
            .putExtra(TerminalService.EXTRA_WORKDIR, intent.getStringExtra(PREFIX + "WORKDIR"))
            .putExtra(TerminalService.EXTRA_BACKGROUND, intent.getBooleanExtra(PREFIX + "BACKGROUND", false))
            .putExtra(TerminalService.EXTRA_RUNNER, intent.getStringExtra(PREFIX + "RUNNER"))
            .putExtra(TerminalService.EXTRA_SESSION_ACTION, intent.getStringExtra(PREFIX + "SESSION_ACTION"))
            .putExtra(TerminalService.EXTRA_PENDING_INTENT, pendingIntent);
        startForegroundService(execute);
    }

    private void sendError(PendingIntent pendingIntent, String error) {
        if (pendingIntent == null) return;
        Bundle result = new Bundle();
        result.putInt("err", 1);
        result.putString("errmsg", error);
        try {
            pendingIntent.send(this, 0, new Intent().putExtra("result", result));
        } catch (PendingIntent.CanceledException e) {
            Log.w("droid-nix", "the caller's PendingIntent was canceled", e);
        }
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

}
