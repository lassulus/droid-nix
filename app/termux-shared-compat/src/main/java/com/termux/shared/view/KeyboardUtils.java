package com.termux.shared.view;

import android.app.Activity;
import android.content.Context;
import android.view.View;
import android.view.WindowManager;
import android.view.inputmethod.InputMethodManager;

public final class KeyboardUtils {

    public static void setSoftKeyboardAlwaysHiddenFlags(Activity activity) {
        activity.getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN);
    }

    public static void showSoftKeyboard(Context context, View view) {
        context.getSystemService(InputMethodManager.class).showSoftInput(view, InputMethodManager.SHOW_IMPLICIT);
    }

}
