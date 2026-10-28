package us.lassul.droidnix;

import android.database.Cursor;
import android.database.MatrixCursor;
import android.os.CancellationSignal;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract.Document;
import android.provider.DocumentsContract.Root;
import android.provider.DocumentsProvider;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;

/**
 * The home directory in Android's Files app and the system file picker. A
 * document id is the file's absolute path.
 */
public final class HomeDocumentsProvider extends DocumentsProvider {

    private static final String[] ROOT_COLUMNS = {Root.COLUMN_ROOT_ID, Root.COLUMN_DOCUMENT_ID, Root.COLUMN_TITLE,
        Root.COLUMN_FLAGS, Root.COLUMN_ICON, Root.COLUMN_AVAILABLE_BYTES};
    private static final String[] DOCUMENT_COLUMNS = {Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME,
        Document.COLUMN_MIME_TYPE, Document.COLUMN_LAST_MODIFIED, Document.COLUMN_FLAGS, Document.COLUMN_SIZE};

    private File home;

    @Override
    public boolean onCreate() {
        home = Shell.home(getContext());
        return true;
    }

    @Override
    public Cursor queryRoots(String[] projection) {
        MatrixCursor result = new MatrixCursor(projection != null ? projection : ROOT_COLUMNS);
        result.newRow()
            .add(Root.COLUMN_ROOT_ID, home.getPath())
            .add(Root.COLUMN_DOCUMENT_ID, home.getPath())
            .add(Root.COLUMN_TITLE, getContext().getString(R.string.app_name))
            .add(Root.COLUMN_FLAGS, Root.FLAG_SUPPORTS_CREATE | Root.FLAG_SUPPORTS_IS_CHILD)
            .add(Root.COLUMN_ICON, R.mipmap.ic_launcher)
            .add(Root.COLUMN_AVAILABLE_BYTES, home.getFreeSpace());
        return result;
    }

    @Override
    public Cursor queryDocument(String documentId, String[] projection) throws FileNotFoundException {
        MatrixCursor result = new MatrixCursor(projection != null ? projection : DOCUMENT_COLUMNS);
        addRow(result, file(documentId));
        return result;
    }

    @Override
    public Cursor queryChildDocuments(String parentDocumentId, String[] projection, String sortOrder)
        throws FileNotFoundException {
        MatrixCursor result = new MatrixCursor(projection != null ? projection : DOCUMENT_COLUMNS);
        File[] children = file(parentDocumentId).listFiles();
        if (children != null) for (File child : children) addRow(result, child);
        return result;
    }

    @Override
    public ParcelFileDescriptor openDocument(String documentId, String mode, CancellationSignal signal)
        throws FileNotFoundException {
        return ParcelFileDescriptor.open(file(documentId), ParcelFileDescriptor.parseMode(mode));
    }

    @Override
    public String createDocument(String parentDocumentId, String mimeType, String displayName)
        throws FileNotFoundException {
        File file = new File(file(parentDocumentId), displayName);
        try {
            boolean created = Document.MIME_TYPE_DIR.equals(mimeType) ? file.mkdir() : file.createNewFile();
            if (!created) throw new FileNotFoundException("cannot create " + file);
        } catch (IOException e) {
            throw new FileNotFoundException(e.getMessage());
        }
        return file.getPath();
    }

    @Override
    public void deleteDocument(String documentId) throws FileNotFoundException {
        try {
            Installer.delete(file(documentId));
        } catch (IOException e) {
            throw new FileNotFoundException(e.getMessage());
        }
    }

    @Override
    public boolean isChildDocument(String parentDocumentId, String documentId) {
        return documentId.startsWith(parentDocumentId + "/");
    }

    private File file(String documentId) throws FileNotFoundException {
        File file = new File(documentId).toPath().normalize().toFile();
        if (!(file.equals(home) || isChildDocument(home.getPath(), file.getPath())) || !file.exists())
            throw new FileNotFoundException(documentId);
        return file;
    }

    private static void addRow(MatrixCursor result, File file) {
        int flags = 0;
        String mimeType;
        if (file.isDirectory()) {
            mimeType = Document.MIME_TYPE_DIR;
            if (file.canWrite()) flags |= Document.FLAG_DIR_SUPPORTS_CREATE;
        } else {
            mimeType = OpenReceiver.mimeType(file);
            if (file.canWrite()) flags |= Document.FLAG_SUPPORTS_WRITE;
        }
        if (file.getParentFile().canWrite()) flags |= Document.FLAG_SUPPORTS_DELETE;
        result.newRow()
            .add(Document.COLUMN_DOCUMENT_ID, file.getPath())
            .add(Document.COLUMN_DISPLAY_NAME, file.getName())
            .add(Document.COLUMN_MIME_TYPE, mimeType)
            .add(Document.COLUMN_LAST_MODIFIED, file.lastModified())
            .add(Document.COLUMN_FLAGS, flags)
            .add(Document.COLUMN_SIZE, file.length());
    }

}
