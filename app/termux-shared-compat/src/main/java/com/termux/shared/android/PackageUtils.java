package com.termux.shared.android;

import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;

public final class PackageUtils {

    public static ApplicationInfo getApplicationInfoForPackage(Context context, String packageName) {
        try {
            return context.getPackageManager().getApplicationInfo(packageName, 0);
        } catch (PackageManager.NameNotFoundException e) {
            return null;
        }
    }

}
