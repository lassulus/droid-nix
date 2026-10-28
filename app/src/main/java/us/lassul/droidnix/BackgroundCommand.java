package us.lassul.droidnix;

import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.util.Log;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Map;

/**
 * Runs a command without a terminal (the "app-shell" runner): Termux:API's
 * notification actions and jobs, and RUN_COMMAND in the background. The
 * result goes to the caller's PendingIntent as the bundle "result" with
 * stdout, stderr, exitCode, err and errmsg, as in Termux.
 */
final class BackgroundCommand {

    /**
     * Binder transactions are limited to 1 MB per process: like Termux, keep
     * each output to this many characters and send its original length.
     */
    private static final int OUTPUT_LIMIT = 100_000;

    /** Runs command on its own thread and then whenDone on it. */
    static void start(Context context, String[] command, String workingDirectory, String stdin,
                      PendingIntent pendingIntent, Runnable whenDone) {
        new Thread(() -> {
            Bundle result = run(context, command, workingDirectory, stdin);
            if (pendingIntent != null) {
                try {
                    pendingIntent.send(context, 0, new Intent().putExtra("result", result));
                } catch (PendingIntent.CanceledException e) {
                    Log.w("droid-nix", "the caller's PendingIntent was canceled", e);
                }
            }
            whenDone.run();
        }, "command " + command[command.length - 1]).start();
    }

    private static Bundle run(Context context, String[] command, String workingDirectory, String stdin) {
        Bundle result = new Bundle();
        try {
            ProcessBuilder builder = new ProcessBuilder(command)
                .directory(new File(workingDirectory != null ? workingDirectory : Shell.home(context).getPath()));
            Map<String, String> environment = builder.environment();
            environment.clear();
            for (String variable : Shell.environment(context, false)) {
                int equals = variable.indexOf('=');
                environment.put(variable.substring(0, equals), variable.substring(equals + 1));
            }
            Process process = builder.start();
            ByteArrayOutputStream stdout = new ByteArrayOutputStream();
            ByteArrayOutputStream stderr = new ByteArrayOutputStream();
            Thread out = copy(process.getInputStream(), stdout);
            Thread err = copy(process.getErrorStream(), stderr);
            try (OutputStream in = process.getOutputStream()) {
                if (stdin != null) in.write(stdin.getBytes(StandardCharsets.UTF_8));
            } catch (IOException e) {
                // The command does not read its input.
            }
            int exitCode = process.waitFor();
            out.join();
            err.join();
            putOutput(result, "stdout", stdout.toString(StandardCharsets.UTF_8.name()));
            putOutput(result, "stderr", stderr.toString(StandardCharsets.UTF_8.name()));
            result.putInt("exitCode", exitCode);
            result.putInt("err", -1);
        } catch (IOException | InterruptedException e) {
            result.putInt("err", 1);
            result.putString("errmsg", e.toString());
        }
        return result;
    }

    private static Thread copy(InputStream in, ByteArrayOutputStream out) {
        Thread thread = new Thread(() -> {
            byte[] buffer = new byte[4096];
            int n;
            try (InputStream input = in) {
                while ((n = input.read(buffer)) != -1) out.write(buffer, 0, n);
            } catch (IOException e) {
                Log.w("droid-nix", "reading a command's output", e);
            }
        });
        thread.start();
        return thread;
    }

    private static void putOutput(Bundle result, String key, String output) {
        result.putString(key, output.length() > OUTPUT_LIMIT ? output.substring(output.length() - OUTPUT_LIMIT) : output);
        result.putString(key + "_original_length", Integer.toString(output.length()));
    }

}
