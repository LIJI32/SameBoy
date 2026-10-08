package io.sameboy.android;

import org.json.JSONException;
import org.json.JSONObject;

/** One ROM in the library. crc32 is the identity (dedup + save key). */
final class LibraryEntry {
    final String uri;         // SAF document uri string
    final String zipEntry;    // null unless the ROM lives inside a zip
    final String displayName; // file name (with extension), for display fallback
    final String title;       // GB_get_rom_title, may be empty
    final String crc32;       // 8-hex uppercase
    boolean favorite;
    long lastPlayed;          // epoch millis, 0 = never

    LibraryEntry(String uri, String zipEntry, String displayName, String title,
                 String crc32, boolean favorite, long lastPlayed) {
        this.uri = uri; this.zipEntry = zipEntry; this.displayName = displayName;
        this.title = title; this.crc32 = crc32; this.favorite = favorite;
        this.lastPlayed = lastPlayed;
    }

    /** Grid label: internal ROM title if present, else the file name. */
    String label() {
        return (title != null && !title.isEmpty()) ? title : displayName;
    }

    JSONObject toJson() throws JSONException {
        JSONObject o = new JSONObject();
        o.put("uri", uri);
        if (zipEntry != null) o.put("zipEntry", zipEntry);
        o.put("displayName", displayName);
        o.put("title", title);
        o.put("crc32", crc32);
        o.put("favorite", favorite);
        o.put("lastPlayed", lastPlayed);
        return o;
    }

    static LibraryEntry fromJson(JSONObject o) {
        return new LibraryEntry(
            o.optString("uri", ""),
            o.has("zipEntry") ? o.optString("zipEntry", null) : null,
            o.optString("displayName", "rom"),
            o.optString("title", ""),
            o.optString("crc32", ""),
            o.optBoolean("favorite", false),
            o.optLong("lastPlayed", 0));
    }
}
