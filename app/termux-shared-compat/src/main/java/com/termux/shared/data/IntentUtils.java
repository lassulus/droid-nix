package com.termux.shared.data;

import android.content.Intent;
import android.os.Bundle;

public final class IntentUtils {

    /** The intent and its extras, for logs. */
    public static String getIntentString(Intent intent) {
        if (intent == null) return "null";
        StringBuilder text = new StringBuilder(intent.toString());
        Bundle extras = intent.getExtras();
        if (extras != null)
            for (String key : extras.keySet()) text.append("\n").append(key).append(": ").append(extras.get(key));
        return text.toString();
    }

}
