package us.lassul.droidnix;

import android.app.Application;
import android.app.NotificationChannel;
import android.app.NotificationManager;

import com.termux.api.util.ResultReturner;

import java.io.File;
import java.io.IOException;
import java.io.PrintWriter;
import java.util.Date;

public final class App extends Application {

    @Override
    public void onCreate() {
        super.onCreate();

        // Keep the last crash where the user can read it.
        Thread.UncaughtExceptionHandler android = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler((thread, e) -> {
            try (PrintWriter log = new PrintWriter(new File(Shell.home(this), "crash_log.txt"))) {
                log.println(new Date() + ", thread " + thread.getName() + ":");
                e.printStackTrace(log);
            } catch (IOException ignored) {
                // No home yet.
            }
            android.uncaughtException(thread, e);
        });

        getSystemService(NotificationManager.class).createNotificationChannel(new NotificationChannel(
            TerminalService.CHANNEL, getString(R.string.notification_channel), NotificationManager.IMPORTANCE_LOW));
        AmServer.start(this);
        // Termux:API sends its results with this context.
        ResultReturner.setContext(this);
    }

}
