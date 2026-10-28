package us.lassul.droidnix;

import android.content.Context;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/** How droid-nix starts programs: where they live, their environment and command line. */
final class Shell {

    /** The bootstrap and, after the first switch, the Nix store: the launcher's / */
    static File usr(Context context) {
        return new File(context.getFilesDir(), "usr");
    }

    static File home(Context context) {
        return new File(context.getFilesDir(), "home");
    }

    /**
     * The environment of sessions and commands: the app's own (ANDROID_*,
     * BOOTCLASSPATH, ... as in adb shell) and ours. The failsafe shell keeps
     * Android's PATH.
     */
    static String[] environment(Context context, boolean failsafe) {
        Map<String, String> environment = new HashMap<>(System.getenv());
        environment.put("HOME", home(context).getPath());
        environment.put("TMPDIR", new File(usr(context), "tmp").getPath());
        environment.put("LANG", "en_US.UTF-8");
        environment.put("TERM", "xterm-256color");
        environment.put("COLORTERM", "truecolor");
        // The login runs the launcher, liblauncher.so, from there.
        environment.put("DROID_NIX_NATIVE_LIB_DIR", context.getApplicationInfo().nativeLibraryDir);
        if (!failsafe) environment.put("PATH", new File(usr(context), "bin").getPath());

        List<String> result = new ArrayList<>();
        for (Map.Entry<String, String> variable : environment.entrySet())
            result.add(variable.getKey() + "=" + variable.getValue());
        return result.toArray(new String[0]);
    }

    /** The login (modules/environment/login), which runs args or the user's shell in the Nix environment. */
    static String[] login(Context context, String... args) {
        return command(new File(usr(context), "bin/login").getPath(), args);
    }

    /**
     * The command line that runs executable with args. The app may not execve
     * its data files (targetSdkVersion 29 and later), so a script is run by
     * its "#!" interpreter, as the kernel would: the login is a
     * /system/bin/sh script.
     */
    static String[] command(String executable, String... args) {
        List<String> command = new ArrayList<>();
        try (InputStream in = new FileInputStream(executable)) {
            if (in.read() == '#' && in.read() == '!') {
                String line = new BufferedReader(new InputStreamReader(in)).readLine();
                if (line != null) command.addAll(Arrays.asList(line.trim().split("\\s+", 2)));
            }
        } catch (IOException e) {
            // Not readable: run it as it is and let exec report the error.
        }
        command.add(executable);
        command.addAll(Arrays.asList(args));
        return command.toArray(new String[0]);
    }

}
