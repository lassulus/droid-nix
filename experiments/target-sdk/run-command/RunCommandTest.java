package us.lassul.droidnix.runcommandtest;

import android.app.Activity;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.util.Log;

/**
 * Sends droid-nix a RUN_COMMAND intent, as Tasker and other apps do, see
 * https://github.com/termux/termux-app/wiki/RUN_COMMAND-Intent.
 *
 * Extras of the activity / the Trigger broadcast:
 *   path        executable, default $PREFIX/bin/login
 *   args        arguments (string array)
 *   background  run without a terminal session
 *   result      ask for the result through a PendingIntent (logged by Result)
 *   id          request code of that PendingIntent
 */
public class RunCommandTest extends Activity {

    static final String TAG = "RunCommandTest";
    static final String PKG = "us.lassul.droidnix";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        send(this, getIntent(), "activity");
        finish();
    }

    public static class Trigger extends BroadcastReceiver {
        @Override
        public void onReceive(Context context, Intent intent) {
            send(context, intent, "receiver");
        }
    }

    public static class Result extends BroadcastReceiver {
        @Override
        public void onReceive(Context context, Intent intent) {
            Bundle result = intent.getBundleExtra("result");
            if (result == null) {
                Log.i(TAG, "result " + intent.getIntExtra("id", -1) + ": no result bundle");
                return;
            }
            Log.i(TAG, "result " + intent.getIntExtra("id", -1)
                + ": exitCode=" + result.getInt("exitCode")
                + " err=" + result.getInt("err")
                + " errmsg=" + result.getString("errmsg")
                + " stdout=[" + result.getString("stdout") + "]"
                + " stderr=[" + result.getString("stderr") + "]");
        }
    }

    static void send(Context context, Intent in, String from) {
        String path = in.getStringExtra("path");
        if (path == null) path = "/data/data/" + PKG + "/files/usr/bin/login";

        Intent intent = new Intent(PKG + ".RUN_COMMAND");
        intent.setClassName(PKG, "com.termux.app.RunCommandService");
        intent.putExtra(PKG + ".RUN_COMMAND_PATH", path);
        intent.putExtra(PKG + ".RUN_COMMAND_ARGUMENTS", in.getStringArrayExtra("args"));
        intent.putExtra(PKG + ".RUN_COMMAND_BACKGROUND", in.getBooleanExtra("background", false));

        if (in.getBooleanExtra("result", false)) {
            int id = in.getIntExtra("id", 0);
            Intent resultIntent = new Intent(context, Result.class).putExtra("id", id);
            // Mutable: droid-nix fills in the result bundle.
            PendingIntent pendingIntent = PendingIntent.getBroadcast(context, id, resultIntent,
                PendingIntent.FLAG_ONE_SHOT | PendingIntent.FLAG_MUTABLE);
            intent.putExtra(PKG + ".RUN_COMMAND_PENDING_INTENT", pendingIntent);
        }

        try {
            context.startForegroundService(intent);
            Log.i(TAG, "sent from " + from + ": " + path);
        } catch (Exception e) {
            Log.e(TAG, "send from " + from + " failed", e);
        }
    }
}
