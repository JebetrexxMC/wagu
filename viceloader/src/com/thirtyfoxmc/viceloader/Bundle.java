package com.thirtyfoxmc.viceloader;

import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.content.res.AssetManager;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.provider.MediaStore;
import android.widget.Toast;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;

/** assets/bundle: libMP (MonetLoader built in) and moonloader/ - exported to Download/ViceLoader. */
final class Bundle {
    private Bundle() {}

    static void list(AssetManager am, String dir, List<String> out) {
        try {
            String[] names = am.list(dir);
            if (names == null || names.length == 0) { out.add(dir); return; }
            for (String n : names) list(am, dir + "/" + n, out);
        } catch (Exception ignored) {}
    }

    static String describe(Context c) {
        List<String> files = new ArrayList<>();
        list(c.getAssets(), "bundle", files);
        StringBuilder sb = new StringBuilder();
        long total = 0;
        for (String f : files) {
            long n = size(c.getAssets(), f);
            total += n;
            if (!f.contains("/moonloader/")) sb.append(f.substring(7)).append("   ").append(n / 1024).append(" KB\n");
        }
        int lua = 0; for (String f : files) if (f.contains("/moonloader/")) lua++;
        sb.append("moonloader/   ").append(lua).append(" files\n");
        sb.append("total ").append(total / (1024 * 1024)).append(" MB");
        return sb.toString();
    }

    private static long size(AssetManager am, String f) {
        try { return am.openFd(f).getLength(); } catch (Exception e) {
            try (InputStream in = am.open(f)) { long n = 0; byte[] b = new byte[65536]; int r; while ((r = in.read(b)) > 0) n += r; return n; } catch (Exception e2) { return 0; }
        }
    }

    static void exportAsync(final Context c) {
        final Context app = c.getApplicationContext();
        final Handler main = new Handler(Looper.getMainLooper());
        Toast.makeText(app, "Exporting to Download/ViceLoader ...", Toast.LENGTH_SHORT).show();
        new Thread(() -> {
            String msg;
            try { int n = export(app); msg = "Exported " + n + " files to Download/ViceLoader"; }
            catch (Exception e) { msg = "Export failed: " + e.getMessage(); }
            final String m = msg;
            main.post(() -> Toast.makeText(app, m, Toast.LENGTH_LONG).show());
        }, "vl-export").start();
    }

    static int export(Context c) throws Exception {
        List<String> files = new ArrayList<>();
        list(c.getAssets(), "bundle", files);
        int n = 0;
        for (String f : files) {
            String rel = f.substring("bundle/".length());
            String sub = rel.contains("/") ? rel.substring(0, rel.lastIndexOf('/')) : "";
            String name = rel.substring(rel.lastIndexOf('/') + 1);
            try (InputStream in = c.getAssets().open(f); OutputStream out = open(c, sub, name)) {
                byte[] b = new byte[1 << 16]; int r;
                while ((r = in.read(b)) > 0) out.write(b, 0, r);
            }
            n++;
        }
        return n;
    }

    private static OutputStream open(Context c, String sub, String name) throws Exception { return open(c, sub, name, "application/octet-stream"); }

    static OutputStream open(Context c, String sub, String name, String mime) throws Exception {
        if (Build.VERSION.SDK_INT >= 29) {
            ContentResolver cr = c.getContentResolver();
            ContentValues v = new ContentValues();
            v.put(MediaStore.MediaColumns.DISPLAY_NAME, name);
            v.put(MediaStore.MediaColumns.MIME_TYPE, mime);
            v.put(MediaStore.MediaColumns.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS + "/ViceLoader" + (sub.isEmpty() ? "" : "/" + sub));
            Uri u = cr.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, v);
            if (u == null) throw new Exception("no Downloads access");
            return cr.openOutputStream(u);
        }
        File d = new File(Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS), "ViceLoader" + (sub.isEmpty() ? "" : "/" + sub));
        if (!d.isDirectory() && !d.mkdirs()) throw new Exception("can't create " + d);
        return new FileOutputStream(new File(d, name));
    }
}
