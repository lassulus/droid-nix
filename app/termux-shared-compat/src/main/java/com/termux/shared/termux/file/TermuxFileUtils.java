package com.termux.shared.termux.file;

import com.termux.shared.errors.Error;
import com.termux.shared.file.FileUtils;
import com.termux.shared.termux.TermuxConstants;

import java.io.File;
import java.io.IOException;

public final class TermuxFileUtils {

    private static final String HOME = "/data/data/" + TermuxConstants.TERMUX_PACKAGE_NAME + "/files/home";

    /** The canonical path of path, which may start with ~/ and, if relative, is under prefix. */
    public static String getCanonicalPath(String path, String prefixForNonAbsolutePath, boolean expandHomeDirectory) {
        if (path == null) path = "";
        if (expandHomeDirectory && (path.equals("~") || path.startsWith("~/"))) path = HOME + path.substring(1);
        File file = path.startsWith("/") ? new File(path) : new File(prefixForNonAbsolutePath != null ? prefixForNonAbsolutePath : "/", path);
        try {
            return file.getCanonicalPath();
        } catch (IOException e) {
            return file.getAbsolutePath();
        }
    }

    /** Checks that the directory exists, or creates it, and that it can be read and written. */
    public static Error validateDirectoryFileExistenceAndPermissions(String label, String path,
                                                                     boolean createDirectoryIfMissing,
                                                                     boolean setPermissions,
                                                                     boolean setMissingPermissionsOnly,
                                                                     boolean ignoreErrorsIfPathIsInParentDirPath,
                                                                     boolean ignoreIfNotExecutable) {
        File directory = new File(path);
        if (createDirectoryIfMissing && !directory.isDirectory() && !directory.mkdirs())
            return new Error("cannot create " + label + " \"" + path + "\"");
        if (!directory.isDirectory()) return new Error(label + " \"" + path + "\" is not a directory");
        return FileUtils.checkMissingFilePermissions(label, path, "rwx", ignoreIfNotExecutable);
    }

}
