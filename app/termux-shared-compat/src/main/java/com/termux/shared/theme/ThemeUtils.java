package com.termux.shared.theme;

import android.content.Context;
import android.content.res.Configuration;
import android.content.res.TypedArray;

public final class ThemeUtils {

    /** Whether the system is in night mode; droid-nix has only NightMode.SYSTEM. */
    public static boolean shouldEnableDarkTheme(Context context, String name) {
        return (context.getResources().getConfiguration().uiMode & Configuration.UI_MODE_NIGHT_MASK)
            == Configuration.UI_MODE_NIGHT_YES;
    }

    public static int getTextColorPrimary(Context context) {
        return color(context, android.R.attr.textColorPrimary);
    }

    public static int getTextColorLink(Context context) {
        return color(context, android.R.attr.textColorLink);
    }

    private static int color(Context context, int attribute) {
        TypedArray array = context.obtainStyledAttributes(new int[]{attribute});
        try {
            return array.getColor(0, 0);
        } finally {
            array.recycle();
        }
    }

}
