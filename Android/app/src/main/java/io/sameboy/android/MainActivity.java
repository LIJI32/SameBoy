package io.sameboy.android;

import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.OpenableColumns;
import android.widget.Toast;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class MainActivity extends DpadActivity {
    private static final int REQ_TREE = 1, REQ_FILE = 2;
    // Process-scoped: a folder scan outlives the Activity (theme toggle / recreation), and its
    // adds must land in the Library the next instance shows and saves — not a dead one whose
    // save() a stale instance then overwrites.
    private static final ExecutorService io = Executors.newSingleThreadExecutor();
    private static final Handler ui = new Handler(Looper.getMainLooper());
    private static Library library;
    private static boolean scanning = false;   // tree scan in flight: adds are in memory, not yet saved
    private static MainActivity current;       // resumed instance, for refresh() from a finished scan
    private LibraryUi.Model model;

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        if (library == null) { library = new Library(this); library.load(); }
        model = LibraryUi.bind(this, new LibraryUi.Callbacks() {
            @Override public void onImportFolder() { pickTree(); }
            @Override public void onOpenRom() { pickFile(); }
            @Override public void onSettings() {
                startActivity(new Intent(MainActivity.this, SettingsActivity.class));
            }
            @Override public void onPlay(LibraryEntry e) { launch(e); }
            @Override public void onToggleFavorite(LibraryEntry e) {
                library.setFavorite(e.crc32, !e.favorite); library.save(); refresh();
            }
            @Override public void onRemove(LibraryEntry e) {
                library.remove(e.crc32); library.save(); refresh();
            }
        });
    }

    @Override protected void onResume() {
        super.onResume();
        current = this;
        // Reloading mid-scan would wipe the scan's unsaved in-memory adds (entries.clear()).
        if (!scanning) library.load();   // pick up a background scan's save / another instance's changes
        refresh();
    }

    @Override protected void onPause() {
        super.onPause();
        if (current == this) current = null;
    }

    private void refresh() { model.setGames(library.listSorted()); }

    private void pickTree() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(i, REQ_TREE);
    }
    private void pickFile() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(i, REQ_FILE);
    }

    @Override protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (res != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        int grant = data.getFlags() & Intent.FLAG_GRANT_READ_URI_PERMISSION;
        try { getContentResolver().takePersistableUriPermission(uri, grant); } catch (Exception ignored) {}

        if (req == REQ_TREE) {
            Toast.makeText(this, R.string.scanning, Toast.LENGTH_SHORT).show();
            scanning = true;
            io.execute(() -> {
                int[] added = {0};
                RomScanner.scanTree(getApplicationContext(), uri, entry ->
                    ui.post(() -> { if (library.add(entry)) added[0]++; }));
                ui.post(() -> {
                    scanning = false;
                    library.save();
                    if (current != null) current.refresh();
                    Toast.makeText(getApplicationContext(), getString(R.string.added_n, added[0]), Toast.LENGTH_SHORT).show();
                });
            });
        } else { // REQ_FILE
            io.execute(() -> {
                String name = queryName(uri);   // SAF query off the main thread (ANR-safe)
                boolean[] got = {false};
                RomScanner.handleFile(getContentResolver(), uri, name, entry -> {
                    got[0] = true;
                    ui.post(() -> { library.add(entry); library.save(); refresh(); launch(entry); });
                });
                if (!got[0]) ui.post(() -> Toast.makeText(this, R.string.not_a_rom, Toast.LENGTH_SHORT).show());
            });
        }
    }

    private void launch(LibraryEntry entry) {
        library.touch(entry.crc32);
        library.save();
        refresh();
        Intent i = new Intent(this, EmulatorActivity.class);
        i.setData(Uri.parse(entry.uri));
        if (entry.zipEntry != null) i.putExtra(EmulatorActivity.EXTRA_ZIP_ENTRY, entry.zipEntry);
        i.putExtra(EmulatorActivity.EXTRA_ROM_KEY, entry.crc32);
        startActivity(i);
    }

    private String queryName(Uri uri) {
        String name = null;
        try (Cursor c = getContentResolver().query(uri,
                new String[]{ OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (c != null && c.moveToFirst()) {
                int i = c.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (i >= 0) name = c.getString(i);
            }
        } catch (Exception ignored) {}
        if (name == null) name = uri.getLastPathSegment();
        return name == null ? "rom" : name;
    }
}
