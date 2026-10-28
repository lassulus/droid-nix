package com.termux.shared.file;

import android.system.ErrnoException;
import android.system.Os;
import android.system.OsConstants;
import android.system.StructStat;

import com.termux.shared.errors.Error;
import com.termux.shared.file.filesystem.FileType;

import java.io.File;
import java.io.IOException;
import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.NoSuchFileException;
import java.util.List;

public final class FileUtils {

    public static String getFileDirname(String path) {
        return path == null ? null : new File(path).getParent();
    }

    public static FileType getFileType(String path, boolean followLinks) {
        try {
            StructStat stat = followLinks ? Os.stat(path) : Os.lstat(path);
            int type = stat.st_mode & OsConstants.S_IFMT;
            if (type == OsConstants.S_IFREG) return FileType.REGULAR;
            if (type == OsConstants.S_IFDIR) return FileType.DIRECTORY;
            if (type == OsConstants.S_IFLNK) return FileType.SYMLINK;
            if (type == OsConstants.S_IFSOCK) return FileType.SOCKET;
            if (type == OsConstants.S_IFCHR) return FileType.CHARACTER;
            if (type == OsConstants.S_IFIFO) return FileType.FIFO;
            if (type == OsConstants.S_IFBLK) return FileType.BLOCK;
            return FileType.UNKNOWN;
        } catch (ErrnoException e) {
            return FileType.NO_EXIST;
        }
    }

    /** Whether path is one of dirPaths or, with ensureUnder, under one of them. */
    public static boolean isPathInDirPaths(String path, List<String> dirPaths, boolean ensureUnder) {
        for (String dirPath : dirPaths)
            if (path.startsWith(dirPath + "/") || (!ensureUnder && path.equals(dirPath))) return true;
        return false;
    }

    public static Error readTextFromFile(String label, String path, Charset charset, StringBuilder text,
                                         boolean ignoreNonExistentFile) {
        try {
            text.append(new String(Files.readAllBytes(new File(path).toPath()), charset != null ? charset : StandardCharsets.UTF_8));
            return null;
        } catch (NoSuchFileException e) {
            return ignoreNonExistentFile ? null : new Error(label + " \"" + path + "\" does not exist");
        } catch (IOException e) {
            return new Error("cannot read " + label + " \"" + path + "\": " + e);
        }
    }

    /** permissions is like "rwx", with "-" for the ones not needed. */
    public static Error checkMissingFilePermissions(String label, String path, String permissions,
                                                    boolean ignoreIfNotExecutable) {
        File file = new File(path);
        if ((permissions.charAt(0) == 'r' && !file.canRead())
            || (permissions.charAt(1) == 'w' && !file.canWrite())
            || (!ignoreIfNotExecutable && permissions.charAt(2) == 'x' && !file.canExecute()))
            return new Error(label + " \"" + path + "\" is missing the permissions \"" + permissions + "\"");
        return null;
    }

}
