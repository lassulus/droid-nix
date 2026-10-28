package com.termux.shared.termux.plugins;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.content.Context;

import java.util.concurrent.atomic.AtomicInteger;

public final class TermuxPluginUtils {

    private static final String CHANNEL = "errors";
    private static final AtomicInteger nextId = new AtomicInteger(1000);

    /** A notification with an error of a command, e.g. of Termux:API's receiver. */
    public static void sendPluginCommandErrorNotification(Context context, String logTag, CharSequence title,
                                                          String notificationText, String message,
                                                          boolean forceNotification, boolean showToast,
                                                          boolean addDeleteIntent) {
        NotificationManager notifications = context.getSystemService(NotificationManager.class);
        notifications.createNotificationChannel(new NotificationChannel(CHANNEL, "Command errors", NotificationManager.IMPORTANCE_HIGH));
        notifications.notify(nextId.getAndIncrement(), new Notification.Builder(context, CHANNEL)
            .setSmallIcon(android.R.drawable.stat_notify_error)
            .setContentTitle(title)
            .setContentText(notificationText)
            .setStyle(new Notification.BigTextStyle().bigText(notificationText + "\n\n" + message))
            .build());
    }

}
