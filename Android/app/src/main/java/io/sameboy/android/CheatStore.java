package io.sameboy.android;

import android.content.Context;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONObject;
import org.json.JSONTokener;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/** Per-ROM cheat list: <filesDir>/cheats/<rom>.json, a JSON array of {code, desc, enabled}. */
public final class CheatStore {
    private CheatStore() {}

    /** Public: exposed through GameMenuDialog.Host, a public Kotlin interface. */
    public static final class Cheat {
        public String code;
        public String desc;
        public boolean enabled;

        Cheat(String code, String desc, boolean enabled) {
            this.code = code;
            this.desc = desc;
            this.enabled = enabled;
        }
    }

    private static File file(Context ctx, String romName) {
        File dir = new File(ctx.getFilesDir(), "cheats");
        if (!dir.exists()) dir.mkdirs();
        return new File(dir, romName + ".json");
    }

    /** Never null; empty mutable list when the file is missing or corrupt. */
    static List<Cheat> load(Context ctx, String romName) {
        List<Cheat> list = new ArrayList<>();
        byte[] data = SaveStore.read(file(ctx, romName));
        if (data == null) return list;
        try {
            JSONArray arr = (JSONArray) new JSONTokener(new String(data, StandardCharsets.UTF_8)).nextValue();
            for (int i = 0; i < arr.length(); i++) {
                JSONObject o = arr.getJSONObject(i);
                list.add(new Cheat(o.getString("code"), o.optString("desc", ""), o.optBoolean("enabled", true)));
            }
        } catch (Exception e) {
            Log.e("SameBoy", "cheats load failed", e);
            list.clear();
        }
        return list;
    }

    static void save(Context ctx, String romName, List<Cheat> list) {
        JSONArray arr = new JSONArray();
        try {
            for (Cheat c : list) {
                arr.put(new JSONObject().put("code", c.code).put("desc", c.desc).put("enabled", c.enabled));
            }
        } catch (Exception e) { Log.e("SameBoy", "cheats serialize failed", e); return; }
        SaveStore.write(file(ctx, romName), arr.toString().getBytes(StandardCharsets.UTF_8));
    }
}
