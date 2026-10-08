package io.sameboy.android;

import android.content.Context;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONTokener;

import java.io.File;
import java.io.FileOutputStream;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;

/** Persistent ROM library backed by a JSON index in internal storage.
 *  Not thread-safe — call from the main thread only. */
final class Library {
    private static final String FILE = "library.json";
    private final Context ctx;
    private final List<LibraryEntry> entries = new ArrayList<>();

    Library(Context ctx) { this.ctx = ctx.getApplicationContext(); }

    void load() {
        entries.clear();
        byte[] data = SaveStore.read(new File(ctx.getFilesDir(), FILE));
        if (data == null) return;
        try {
            JSONArray arr = (JSONArray) new JSONTokener(new String(data, "UTF-8")).nextValue();
            for (int i = 0; i < arr.length(); i++) entries.add(LibraryEntry.fromJson(arr.getJSONObject(i)));
        } catch (Exception e) {
            Log.e("SameBoy", "library load failed", e);
        }
    }

    void save() {
        JSONArray arr = new JSONArray();
        try {
            for (LibraryEntry e : entries) arr.put(e.toJson());
        } catch (Exception e) { Log.e("SameBoy", "library serialize failed", e); return; }
        File dir = ctx.getFilesDir();
        File tmp = new File(dir, FILE + ".tmp");
        File dst = new File(dir, FILE);
        try (FileOutputStream out = new FileOutputStream(tmp)) {
            out.write(arr.toString().getBytes("UTF-8"));
        } catch (Exception e) { Log.e("SameBoy", "library write failed", e); return; }
        if (!tmp.renameTo(dst)) {
            dst.delete();
            if (!tmp.renameTo(dst)) Log.e("SameBoy", "library rename failed");
        }
    }

    /** Add unless a same-crc32 entry exists. Returns true if newly added. A duplicate keeps
     *  its favorite/lastPlayed but takes the new location: SAF document URIs are path-based,
     *  so a renamed/moved ROM re-imported would otherwise keep a dead URI forever. */
    boolean add(LibraryEntry e) {
        if (e.crc32 == null || e.crc32.isEmpty()) return false;
        for (int i = 0; i < entries.size(); i++) {
            LibraryEntry x = entries.get(i);
            if (!e.crc32.equals(x.crc32)) continue;
            entries.set(i, new LibraryEntry(e.uri, e.zipEntry, e.displayName, e.title, e.crc32, x.favorite, x.lastPlayed));
            return false;
        }
        entries.add(e);
        return true;
    }

    void setFavorite(String crc32, boolean fav) {
        for (LibraryEntry e : entries) if (e.crc32.equals(crc32)) { e.favorite = fav; return; }
    }

    void remove(String crc32) {
        for (int i = 0; i < entries.size(); i++)
            if (entries.get(i).crc32.equals(crc32)) { entries.remove(i); return; }
    }

    void touch(String crc32) {
        for (LibraryEntry e : entries)
            if (e.crc32.equals(crc32)) { e.lastPlayed = System.currentTimeMillis(); return; }
    }

    List<LibraryEntry> listSorted() {
        List<LibraryEntry> out = new ArrayList<>(entries);
        Collections.sort(out, new Comparator<LibraryEntry>() {
            @Override public int compare(LibraryEntry a, LibraryEntry b) {
                if (a.favorite != b.favorite) return a.favorite ? -1 : 1;
                if (a.lastPlayed != b.lastPlayed) return a.lastPlayed > b.lastPlayed ? -1 : 1;
                return a.label().compareToIgnoreCase(b.label());
            }
        });
        return out;
    }

    boolean isEmpty() { return entries.isEmpty(); }
}
