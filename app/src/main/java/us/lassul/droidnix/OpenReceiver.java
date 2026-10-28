package us.lassul.droidnix;

import android.content.BroadcastReceiver;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import android.util.Log;
import android.webkit.MimeTypeMap;

import java.io.File;
import java.io.FileNotFoundException;

/**
 * termux-open and xdg-open (pkgs/android-integration/termux-tools.nix): opens
 * a file or URL (the intent's data) in another app, with ACTION_VIEW or
 * ACTION_SEND, the extras "chooser" and "content-type". Files are shared as
 * content://us.lassul.droidnix.files/PATH with a read grant.
 */
public final class OpenReceiver extends BroadcastReceiver {

    @Override
    public void onReceive(Context context, Intent intent) {
        Uri data = intent.getData();
        if (data == null) return;
        String action = Intent.ACTION_SEND.equals(intent.getAction()) ? Intent.ACTION_SEND : Intent.ACTION_VIEW;
        Intent open = new Intent(action);

        File file = data.getScheme() == null || data.getScheme().equals("file") ? new File(data.getPath()) : null;
        if (file == null) {
            if (action.equals(Intent.ACTION_SEND)) open.setType("text/plain").putExtra(Intent.EXTRA_TEXT, data.toString());
            else open.setData(data);
        } else {
            if (!file.isFile()) {
                Log.e("droid-nix", "termux-open: no such file: " + file);
                return;
            }
            String type = intent.getStringExtra("content-type");
            if (type == null) type = mimeType(file);
            Uri uri = new Uri.Builder().scheme("content").authority(FilesProvider.AUTHORITY).path(file.getAbsolutePath()).build();
            if (action.equals(Intent.ACTION_SEND)) open.setType(type).putExtra(Intent.EXTRA_STREAM, uri);
            else open.setDataAndType(uri, type);
            open.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        }
        if (intent.getBooleanExtra("chooser", false)) open = Intent.createChooser(open, null);
        open.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        try {
            context.startActivity(open);
        } catch (RuntimeException e) {
            Log.e("droid-nix", "termux-open: " + data, e);
        }
    }

    static String mimeType(File file) {
        String name = file.getName();
        String extension = name.substring(name.lastIndexOf('.') + 1).toLowerCase();
        String type = MimeTypeMap.getSingleton().getMimeTypeFromExtension(extension);
        return type != null ? type : "application/octet-stream";
    }

    /** The files that termux-open shares: read only, and only through the grants it gives. */
    public static final class FilesProvider extends android.content.ContentProvider {

        static final String AUTHORITY = "us.lassul.droidnix.files";

        @Override
        public boolean onCreate() {
            return true;
        }

        @Override
        public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
            return ParcelFileDescriptor.open(new File(uri.getPath()), ParcelFileDescriptor.MODE_READ_ONLY);
        }

        @Override
        public Cursor query(Uri uri, String[] projection, String selection, String[] selectionArgs, String sortOrder) {
            File file = new File(uri.getPath());
            MatrixCursor cursor = new MatrixCursor(new String[]{OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE});
            cursor.addRow(new Object[]{file.getName(), file.length()});
            return cursor;
        }

        @Override
        public String getType(Uri uri) {
            return mimeType(new File(uri.getPath()));
        }

        @Override
        public Uri insert(Uri uri, ContentValues values) {
            throw new UnsupportedOperationException();
        }

        @Override
        public int delete(Uri uri, String selection, String[] selectionArgs) {
            throw new UnsupportedOperationException();
        }

        @Override
        public int update(Uri uri, ContentValues values, String selection, String[] selectionArgs) {
            throw new UnsupportedOperationException();
        }

    }

}
