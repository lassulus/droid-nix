package com.termux.shared.android;

import android.app.Activity;
import android.content.Context;
import android.content.pm.PackageManager;

public final class PermissionUtils {

    public static boolean checkPermission(Context context, String permission) {
        return context.checkSelfPermission(permission) == PackageManager.PERMISSION_GRANTED;
    }

    public static void requestPermissions(Activity activity, String[] permissions, int requestCode) {
        activity.requestPermissions(permissions, requestCode);
    }

}
