package com.termux.shared.android;

import android.app.PendingIntent;

public final class PendingIntentUtils {

    public static int getPendingIntentImmutableFlag() {
        return PendingIntent.FLAG_IMMUTABLE;
    }

    public static int getPendingIntentMutableFlag() {
        return PendingIntent.FLAG_MUTABLE;
    }

}
