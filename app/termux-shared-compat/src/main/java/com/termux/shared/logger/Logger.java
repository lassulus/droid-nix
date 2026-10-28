package com.termux.shared.logger;

import android.util.Log;

/** Logs to logcat. */
public final class Logger {

    public static void logError(String tag, String message) {
        Log.e(tag, message);
    }

    /** An error that may contain private data: logcat is only readable by the app and adb. */
    public static void logErrorPrivate(String tag, String message) {
        Log.e(tag, message);
    }

    public static void logWarn(String tag, String message) {
        Log.w(tag, message);
    }

    public static void logInfo(String tag, String message) {
        Log.i(tag, message);
    }

    public static void logDebug(String tag, String message) {
        Log.d(tag, message);
    }

    public static void logVerbose(String tag, String message) {
        Log.v(tag, message);
    }

    public static void logStackTraceWithMessage(String tag, String message, Throwable throwable) {
        Log.e(tag, message, throwable);
    }

    public static void logStackTrace(String tag, Throwable throwable) {
        Log.e(tag, "", throwable);
    }

    public static String getStackTraceString(Throwable throwable) {
        return Log.getStackTraceString(throwable);
    }

    public static String getMessageAndStackTraceString(String message, Throwable throwable) {
        return message + ":\n" + Log.getStackTraceString(throwable);
    }

}
