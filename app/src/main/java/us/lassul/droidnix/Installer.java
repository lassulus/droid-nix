package us.lassul.droidnix;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.os.Environment;
import android.system.ErrnoException;
import android.system.Os;
import android.util.Log;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.file.DirectoryStream;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * Unpacks the bootstrap (../bootstrap) from the APK asset bootstrap-aarch64.zip
 * into files/usr on the first start. Zip entries carry no modes or symlinks:
 * EXECUTABLES.txt lists the executables, SYMLINKS.txt the symlinks as
 * "target←link". It is unpacked into files/usr-staging and renamed, so an
 * interrupted install starts over.
 */
final class Installer {

    static void installIfNeeded(Activity activity, Runnable whenDone) {
        File usr = Shell.usr(activity);
        if (usr.isDirectory()) {
            whenDone.run();
            return;
        }
        AlertDialog progress = new AlertDialog.Builder(activity)
            .setMessage(R.string.installing)
            .setCancelable(false)
            .show();
        new Thread(() -> {
            try {
                install(activity, usr);
                activity.runOnUiThread(() -> {
                    progress.dismiss();
                    whenDone.run();
                });
            } catch (Exception e) {
                Log.e("droid-nix", "installing the bootstrap failed", e);
                activity.runOnUiThread(() -> {
                    progress.dismiss();
                    new AlertDialog.Builder(activity)
                        .setTitle(R.string.install_failed)
                        .setMessage(Log.getStackTraceString(e))
                        .setPositiveButton(android.R.string.ok, (dialog, which) -> activity.finish())
                        .setCancelable(false)
                        .show();
                });
            }
        }).start();
    }

    private static void install(Activity activity, File usr) throws Exception {
        File staging = new File(activity.getFilesDir(), "usr-staging");
        delete(staging);

        List<String> executables = new ArrayList<>();
        List<String[]> symlinks = new ArrayList<>();
        byte[] buffer = new byte[65536];
        try (ZipInputStream zip = new ZipInputStream(activity.getAssets().open("bootstrap-aarch64.zip"))) {
            ZipEntry entry;
            while ((entry = zip.getNextEntry()) != null) {
                String name = entry.getName();
                if (name.equals("EXECUTABLES.txt") || name.equals("SYMLINKS.txt")) {
                    BufferedReader lines = new BufferedReader(new InputStreamReader(zip));
                    String line;
                    while ((line = lines.readLine()) != null) {
                        if (name.equals("EXECUTABLES.txt")) executables.add(line);
                        else symlinks.add(line.split("←", 2));
                    }
                    continue;
                }
                File file = new File(staging, name);
                if (entry.isDirectory()) {
                    mkdirs(file);
                    continue;
                }
                mkdirs(file.getParentFile());
                try (OutputStream out = new FileOutputStream(file)) {
                    int n;
                    while ((n = zip.read(buffer)) != -1) out.write(buffer, 0, n);
                }
            }
        }

        for (String executable : executables)
            Os.chmod(new File(staging, executable).getPath(), 0700);
        for (String[] symlink : symlinks) {
            File link = new File(staging, symlink[1]);
            mkdirs(link.getParentFile());
            Os.symlink(symlink[0], link.getPath());
        }

        if (!staging.renameTo(usr)) throw new IOException("cannot rename " + staging + " to " + usr);
        mkdirs(Shell.home(activity));
    }

    /**
     * ~/storage (termux-setup-storage): links to the shared storage, its
     * common directories, and the app's directories on other volumes
     * (external-N, media-N).
     */
    static void setupStorage(Context context) throws IOException, ErrnoException {
        File storage = new File(Shell.home(context), "storage");
        delete(storage);
        mkdirs(storage);
        File shared = Environment.getExternalStorageDirectory();
        Os.symlink(shared.getPath(), new File(storage, "shared").getPath());
        String[][] directories = {{"documents", Environment.DIRECTORY_DOCUMENTS}, {"downloads", Environment.DIRECTORY_DOWNLOADS},
            {"dcim", Environment.DIRECTORY_DCIM}, {"pictures", Environment.DIRECTORY_PICTURES}, {"music", Environment.DIRECTORY_MUSIC},
            {"movies", Environment.DIRECTORY_MOVIES}, {"podcasts", Environment.DIRECTORY_PODCASTS}, {"audiobooks", "Audiobooks"}};
        for (String[] directory : directories)
            Os.symlink(new File(shared, directory[1]).getPath(), new File(storage, directory[0]).getPath());
        File[] external = context.getExternalFilesDirs(null);
        for (int i = 1; i < external.length; i++)
            if (external[i] != null) Os.symlink(external[i].getPath(), new File(storage, "external-" + i).getPath());
        File[] media = context.getExternalMediaDirs();
        for (int i = 1; i < media.length; i++)
            if (media[i] != null) Os.symlink(media[i].getPath(), new File(storage, "media-" + i).getPath());
    }

    private static void mkdirs(File directory) throws IOException {
        if (!directory.isDirectory() && !directory.mkdirs()) throw new IOException("cannot create " + directory);
    }

    /** Deletes file and, unless it is a symlink, what is in it. */
    static void delete(File file) throws IOException {
        Path path = file.toPath();
        if (Files.isDirectory(path, LinkOption.NOFOLLOW_LINKS)) {
            try (DirectoryStream<Path> children = Files.newDirectoryStream(path)) {
                for (Path child : children) delete(child.toFile());
            }
        }
        Files.deleteIfExists(path);
    }

}
