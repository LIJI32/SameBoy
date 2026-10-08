package io.sameboy.android;

import android.content.ContentResolver;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.util.ArrayDeque;

/** Recursively scans a SAF tree for GB/GBC/zip ROMs, computes title+CRC via
 *  NativeBridge.nativeRomInfo, and reports each valid ROM through a Sink.
 *  Blocking; call on a background thread. */
final class RomScanner {
    interface Sink { void onRom(LibraryEntry entry); }

    private RomScanner() {}

    static void scanTree(Context ctx, Uri treeUri, Sink sink) {
        ContentResolver cr = ctx.getContentResolver();
        ArrayDeque<String> stack = new ArrayDeque<>();
        stack.push(DocumentsContract.getTreeDocumentId(treeUri));
        int guard = 0;
        while (!stack.isEmpty() && guard++ < 100000) {
            String docId = stack.pop();
            Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, docId);
            try (Cursor c = cr.query(children, new String[]{
                    DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                    DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                    DocumentsContract.Document.COLUMN_MIME_TYPE }, null, null, null)) {
                if (c == null) continue;
                while (c.moveToNext()) {
                    String childId = c.getString(0);
                    String name = c.getString(1);
                    String mime = c.getString(2);
                    if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                        stack.push(childId);
                    } else {
                        Uri docUri = DocumentsContract.buildDocumentUriUsingTree(treeUri, childId);
                        handleFile(cr, docUri, name, sink);
                    }
                }
            } catch (Exception ignored) {}
        }
    }

    /** Handle one document: add if it is (or contains) a valid GB/GBC ROM. */
    static void handleFile(ContentResolver cr, Uri docUri, String name, Sink sink) {
        if (name == null) return;
        String lower = name.toLowerCase(java.util.Locale.ROOT);
        if (lower.endsWith(".gb") || lower.endsWith(".gbc")) {
            addRom(sink, readAll(cr, docUri), docUri.toString(), null, name);
        } else if (lower.endsWith(".zip")) {
            String entry;
            try (InputStream in = cr.openInputStream(docUri)) { entry = ZipRoms.firstRomEntry(in); }
            catch (Exception e) { entry = null; }
            if (entry == null) return;
            byte[] rom;
            try (InputStream in = cr.openInputStream(docUri)) { rom = ZipRoms.extract(in, entry); }
            catch (Exception e) { rom = null; }
            addRom(sink, rom, docUri.toString(), entry, name);
        }
    }

    private static void addRom(Sink sink, byte[] rom, String uri, String zipEntry, String name) {
        if (rom == null || rom.length < 0x150) return;
        String[] info = NativeBridge.nativeRomInfo(rom);
        if (info == null) return;   // not a valid ROM
        sink.onRom(new LibraryEntry(uri, zipEntry, name, info[0], info[1], false, 0));
    }

    private static byte[] readAll(ContentResolver cr, Uri uri) {
        try (InputStream in = cr.openInputStream(uri)) {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[65536]; int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            return out.toByteArray();
        } catch (Exception e) { return null; }
    }
}
