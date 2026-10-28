package us.lassul.droidnix;

import android.content.Context;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.Properties;

/**
 * ~/.droid-nix/droid-nix.properties: the terminal's settings, a subset of
 * Termux's termux.properties with the same keys and values. A missing or
 * invalid value means the default.
 */
final class Props {

    static File file(Context context) {
        return new File(new File(Shell.home(context), ".droid-nix"), "droid-nix.properties");
    }

    private final Properties properties = new Properties();

    /** Loads the file; a missing file gives the defaults. */
    Props(Context context) throws IOException {
        File file = file(context);
        if (file.exists()) try (InputStream in = new FileInputStream(file)) {
            properties.load(in);
        }
    }

    /** The defaults, if the file cannot be read. */
    Props() {
    }

    String get(String key, String defaultValue) {
        return properties.getProperty(key, defaultValue).trim();
    }

    boolean is(String key, String value) {
        return get(key, "").equalsIgnoreCase(value);
    }

    boolean getBoolean(String key, boolean defaultValue) {
        String value = get(key, "");
        if (value.equalsIgnoreCase("true")) return true;
        if (value.equalsIgnoreCase("false")) return false;
        return defaultValue;
    }

    Integer getInteger(String key) {
        try {
            return Integer.valueOf(get(key, ""));
        } catch (NumberFormatException e) {
            return null;
        }
    }

    float getFloat(String key, float defaultValue) {
        try {
            return Float.parseFloat(get(key, ""));
        } catch (NumberFormatException e) {
            return defaultValue;
        }
    }

    /** A shortcut such as "ctrl + t": its character, or -1. */
    int getShortcut(String key) {
        String value = get(key, "").toLowerCase().replace(" ", "");
        return value.matches("ctrl\\+.") ? value.codePointAt(5) : -1;
    }

}
