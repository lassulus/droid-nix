package com.termux.shared.termux.settings.preferences;

import android.content.Context;
import android.content.SharedPreferences;

/** Termux:API's last PendingIntent request code, so that its PendingIntents stay unique. */
public final class TermuxAPIAppSharedPreferences {

    private static final String KEY = "last_pending_intent_request_code";

    private final SharedPreferences preferences;

    private TermuxAPIAppSharedPreferences(Context context) {
        preferences = context.getSharedPreferences("termux_api", Context.MODE_PRIVATE);
    }

    public static TermuxAPIAppSharedPreferences build(Context context) {
        return new TermuxAPIAppSharedPreferences(context);
    }

    public int getLastPendingIntentRequestCode() {
        return preferences.getInt(KEY, TermuxPreferenceConstants.TERMUX_API_APP.DEFAULT_VALUE_KEY_LAST_PENDING_INTENT_REQUEST_CODE);
    }

    public void setLastPendingIntentRequestCode(int code) {
        preferences.edit().putInt(KEY, code).apply();
    }

}
