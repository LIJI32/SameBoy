package io.sameboy.android;

import android.app.Activity;
import android.content.ContentValues;
import android.content.Intent;
import android.graphics.Bitmap;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.MediaStore;
import android.widget.Toast;

import androidx.core.content.FileProvider;

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStream;

/** Shows the accumulated Game Boy Printer feed; save to Pictures / share PNG / clear.
 *  Reads pixels via NativeBridge.nativePrinterFeed on the singleton session ctx. */
public final class PrinterFeedActivity extends DpadActivity {
    public static final String EXTRA_CTX = "io.sameboy.ctx";
    private static final int REQ_WRITE = 71;   // pre-29 save-to-Pictures permission
    private long ctx;
    private Bitmap bitmap;

    @Override public void onRequestPermissionsResult(int req, String[] p, int[] r) {
        super.onRequestPermissionsResult(req, p, r);
        if (req == REQ_WRITE && r.length > 0
                && r[0] == android.content.pm.PackageManager.PERMISSION_GRANTED) {
            saveToPictures();   // permission now held; retry
        } else if (req == REQ_WRITE) {
            Toast.makeText(this, "Save failed", Toast.LENGTH_SHORT).show();
        }
    }

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        // Process-death restore: the native session (raw ctx pointer) is gone and the
        // Intent extra is stale — dereferencing it would SIGSEGV. Bail to the launcher.
        if (b != null) { finish(); return; }
        ctx = getIntent().getLongExtra(EXTRA_CTX, 0);
        // Stale extra (process death is caught above, but also require the session be live).
        if (ctx == 0 || ctx != EmulatorActivity.activeCtx) { finish(); return; }
        bitmap = buildBitmap();

        PrinterUi.bind(this, bitmap, new PrinterUi.Callbacks() {
            @Override public void onSave() { saveToPictures(); }
            @Override public void onShare() { sharePng(); }
            @Override public void onClear() {
                // Emulator may have been destroyed beneath us; its ctx is then freed (UAF).
                if (ctx == EmulatorActivity.activeCtx) NativeBridge.nativePrinterClear(ctx);
                finish();
            }
            @Override public void onBack() { finish(); }
        });
    }

    private Bitmap buildBitmap() {
        int[] px = NativeBridge.nativePrinterFeed(ctx);
        if (px == null || px.length < 160) return null;
        int rows = px.length / 160;
        Bitmap bmp = Bitmap.createBitmap(160, rows, Bitmap.Config.ARGB_8888);
        bmp.setPixels(px, 0, 160, 0, 0, 160, rows);
        return bmp;
    }

    private void saveToPictures() {
        if (bitmap == null) return;
        String name = "SameBoy_" + System.currentTimeMillis() + ".png";
        try {
            if (Build.VERSION.SDK_INT >= 29) {
                ContentValues cv = new ContentValues();
                cv.put(MediaStore.Images.Media.DISPLAY_NAME, name);
                cv.put(MediaStore.Images.Media.MIME_TYPE, "image/png");
                cv.put(MediaStore.Images.Media.RELATIVE_PATH, "Pictures/SameBoy");
                Uri uri = getContentResolver().insert(
                        MediaStore.Images.Media.EXTERNAL_CONTENT_URI, cv);
                if (uri != null) {
                    OutputStream os = getContentResolver().openOutputStream(uri);
                    bitmap.compress(Bitmap.CompressFormat.PNG, 100, os);
                    os.close();
                    Toast.makeText(this, R.string.saved_to_pictures, Toast.LENGTH_SHORT).show();
                }
            } else {
                if (checkSelfPermission(android.Manifest.permission.WRITE_EXTERNAL_STORAGE)
                        != android.content.pm.PackageManager.PERMISSION_GRANTED) {
                    requestPermissions(new String[]{ android.Manifest.permission.WRITE_EXTERNAL_STORAGE }, REQ_WRITE);
                    return;   // onRequestPermissionsResult retries the save if granted
                }
                File dir = new File(android.os.Environment.getExternalStoragePublicDirectory(
                        android.os.Environment.DIRECTORY_PICTURES), "SameBoy");
                dir.mkdirs();
                File f = new File(dir, name);
                FileOutputStream os = new FileOutputStream(f);
                bitmap.compress(Bitmap.CompressFormat.PNG, 100, os);
                os.close();
                // Pre-29 there is no MediaStore insert: without a scan, Gallery/Photos never show it.
                android.media.MediaScannerConnection.scanFile(this, new String[]{ f.getPath() }, new String[]{ "image/png" }, null);
                Toast.makeText(this, R.string.saved_to_pictures, Toast.LENGTH_SHORT).show();
            }
        } catch (Exception e) {
            Toast.makeText(this, "Save failed", Toast.LENGTH_SHORT).show();
        }
    }

    private void sharePng() {
        if (bitmap == null) return;
        try {
            File dir = new File(getCacheDir(), "shared");
            dir.mkdirs();
            File f = new File(dir, "printout.png");
            FileOutputStream os = new FileOutputStream(f);
            bitmap.compress(Bitmap.CompressFormat.PNG, 100, os);
            os.close();
            Uri uri = FileProvider.getUriForFile(this, getPackageName() + ".fileprovider", f);
            Intent i = new Intent(Intent.ACTION_SEND);
            i.setType("image/png");
            i.putExtra(Intent.EXTRA_STREAM, uri);
            i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            startActivity(Intent.createChooser(i, getString(R.string.share)));
        } catch (Exception e) {
            Toast.makeText(this, "Share failed", Toast.LENGTH_SHORT).show();
        }
    }
}
