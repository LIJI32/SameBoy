package io.sameboy.android;

import android.content.Context;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.RandomAccessFile;

final class SaveStore {
    private SaveStore() {}

    /** External app storage when mounted, else internal — never null (fixes M1 NPE). */
    private static File subDir(Context ctx, String name) {
        File base = ctx.getExternalFilesDir(null);
        if (base == null) base = ctx.getFilesDir();
        File dir = new File(base, name);
        if (!dir.exists()) dir.mkdirs();
        return dir;
    }

    static File savFile(Context ctx, String romName) {
        return new File(subDir(ctx, "saves"), romName + ".sav");
    }

    /** Save-state slot file: states/<rom>.s<slot> (mirrors iOS GBROMManager). */
    static File stateFile(Context ctx, String romName, int slot) {
        return new File(subDir(ctx, "states"), romName + ".s" + slot);
    }

    /** Slot thumbnail PNG, sibling of the state file. */
    static File stateThumb(Context ctx, String romName, int slot) {
        return new File(subDir(ctx, "states"), romName + ".s" + slot + ".png");
    }

    /** Auto-save written on pause; offered as "Continue" at the next launch. */
    static File autoStateFile(Context ctx, String romName) {
        return new File(subDir(ctx, "states"), romName + ".auto");
    }

    static byte[] read(File f) {
        if (!f.exists()) return null;
        try (RandomAccessFile raf = new RandomAccessFile(f, "r")) {
            byte[] b = new byte[(int) raf.length()];
            raf.readFully(b);
            return b;
        } catch (IOException e) { return null; }
    }

    /** Atomic: tmp + fsync + rename, so a kill mid-write leaves the previous file intact
     *  (an in-place truncate would destroy the old state/battery before the new bytes land). */
    static boolean write(File f, byte[] data) {
        if (data == null) return false;
        File tmp = new File(f.getPath() + ".tmp");
        try (FileOutputStream out = new FileOutputStream(tmp)) {
            out.write(data);
            out.getFD().sync();
        } catch (IOException e) {
            android.util.Log.e("SameBoy", "write failed: " + f, e);
            tmp.delete();
            return false;
        }
        if (tmp.renameTo(f)) return true;
        f.delete();   // rename over an existing file fails on some filesystems; retry once
        if (tmp.renameTo(f)) return true;
        android.util.Log.e("SameBoy", "rename failed: " + f);
        return false;
    }
}
