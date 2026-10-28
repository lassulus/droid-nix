package com.termux.shared.net.uri;

import android.net.Uri;

public final class UriUtils {

    /** content://authority/path, with the path as it is (not parsed). */
    public static Uri getContentUri(String authority, String path) {
        return new Uri.Builder().scheme("content").authority(authority).path(path).build();
    }

}
