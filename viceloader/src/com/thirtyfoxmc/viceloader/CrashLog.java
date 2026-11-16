package com.thirtyfoxmc.viceloader;

import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Context;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.Process;
import android.widget.Toast;

import java.io.BufferedReader;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.List;
import java.util.Locale;

/**
 * 4.13.64: overlay crash log. Java crashes (uncaught exceptions), native crashes (vl_crash.cpp signal handler
 * writes files/crash_native.txt) and Android's own exit reasons (API 30+: native tombstone / ANR trace) are kept
 * in the app's files folder and copied to Download/ViceLoader/crash_<time>.txt on the next start.
 * "Save log to Downloads" on the start screen writes the current log (logcat of this app) at any time.
 */
final class CrashLog {
    private CrashLog() {}
    private static boolean sInstalled;
    static final String JAVA = "crash_java.txt", NATIVE = "crash_native.txt", SEEN = "crash_seen.txt";

    static void install(final Context c) {
        if (sInstalled) return;
        sInstalled = true;
        final Context app = c.getApplicationContext();
        final Thread.UncaughtExceptionHandler prev = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler((t, e) -> {
            try {
                StringWriter sw = new StringWriter();
                PrintWriter pw = new PrintWriter(sw);
                pw.println(header(app, "Java crash in thread \"" + t.getName() + "\""));
                e.printStackTrace(pw);
                pw.println();
                pw.println("---- logcat (this app) ----");
                pw.println(logcat(400));
                pw.flush();
                write(new File(app.getFilesDir(), JAVA), sw.toString());
            } catch (Throwable ignored) {}
            if (prev != null) prev.uncaughtException(t, e);
            else { Process.killProcess(Process.myPid()); System.exit(10); }
        });
    }

    static String header(Context c, String what) {
        String ver = "?";
        try { ver = c.getPackageManager().getPackageInfo(c.getPackageName(), 0).versionName; } catch (Throwable ignored) {}
        return "VICE Loader " + ver + " - " + what + "\n"
             + "time: " + new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(new Date()) + "\n"
             + "device: " + Build.MANUFACTURER + " " + Build.MODEL + " (" + Build.DEVICE + "), Android " + Build.VERSION.RELEASE
             + " (API " + Build.VERSION.SDK_INT + "), ABI " + (Build.SUPPORTED_ABIS.length > 0 ? Build.SUPPORTED_ABIS[0] : "?") + "\n"
             + "overlay permission: " + OverlayService.canDraw(c) + "\n";
    }

    /** the last lines of this app's own log (any app may read its own logcat) */
    static String logcat(int lines) {
        try {
            java.lang.Process p = Runtime.getRuntime().exec(new String[] { "logcat", "-d", "-v", "threadtime", "-t", String.valueOf(lines), "--pid=" + Process.myPid() });
            StringBuilder sb = new StringBuilder();
            BufferedReader r = new BufferedReader(new InputStreamReader(p.getInputStream()));
            for (String l; (l = r.readLine()) != null; ) sb.append(l).append('\n');
            return sb.toString();
        } catch (Throwable e) { return "(logcat failed: " + e + ")"; }
    }

    /** on start: copies any saved crash (Java, native, Android exit reason) to Download/ViceLoader */
    static void exportPending(final Context c) {
        new Thread(() -> {
            StringBuilder all = new StringBuilder();
            File dir = c.getFilesDir();
            for (String n : new String[] { JAVA, NATIVE }) {
                File f = new File(dir, n);
                if (f.isFile() && f.length() > 0) {
                    all.append(read(f)).append("\n\n");
                    f.delete();
                }
            }
            String exit = exitReasons(c);
            if (exit != null) all.append(exit);
            if (all.length() == 0) return;
            String name = "crash_" + new SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(new Date()) + ".txt";
            String msg;
            try { save(c, name, all.toString()); msg = "VICE Loader crashed last time - log saved to Download/ViceLoader/" + name; }
            catch (Throwable e) { write(new File(dir, name), all.toString()); msg = "VICE Loader crash log: " + e.getMessage(); }
            final String m = msg;
            new Handler(Looper.getMainLooper()).post(() -> Toast.makeText(c, m, Toast.LENGTH_LONG).show());
        }, "vl-crashlog").start();
    }

    /** API 30+: the system's record of how the overlay process ended (crash, native crash, ANR) - once per exit */
    private static String exitReasons(Context c) {
        if (Build.VERSION.SDK_INT < 30) return null;
        try {
            ActivityManager am = (ActivityManager) c.getSystemService(Context.ACTIVITY_SERVICE);
            List<ApplicationExitInfo> list = am.getHistoricalProcessExitReasons(null, 0, 5);
            File seenF = new File(c.getFilesDir(), SEEN);
            long seen = 0;
            try { seen = Long.parseLong(read(seenF).trim()); } catch (Throwable ignored) {}
            long newest = seen;
            StringBuilder sb = new StringBuilder();
            for (ApplicationExitInfo e : list) {
                if (e.getTimestamp() <= seen) continue;
                newest = Math.max(newest, e.getTimestamp());
                int r = e.getReason();
                if (r != ApplicationExitInfo.REASON_CRASH && r != ApplicationExitInfo.REASON_CRASH_NATIVE && r != ApplicationExitInfo.REASON_ANR) continue;
                sb.append(header(c, "Android exit record")).append("reason: ")
                  .append(r == ApplicationExitInfo.REASON_CRASH ? "Java crash" : r == ApplicationExitInfo.REASON_CRASH_NATIVE ? "native crash" : "ANR (not responding)")
                  .append("\nprocess: ").append(e.getProcessName()).append("  at ")
                  .append(new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(new Date(e.getTimestamp())))
                  .append("\ndescription: ").append(e.getDescription()).append("\n");
                if (r == ApplicationExitInfo.REASON_ANR) {   // text trace; native tombstones are binary protobuf on Android 12+
                    try (InputStream in = e.getTraceInputStream()) {
                        if (in != null) { String t = new String(readAll(in), "UTF-8"); sb.append(t.length() > 60000 ? t.substring(0, 60000) : t).append('\n'); }
                    } catch (Throwable ignored) {}
                }
                sb.append('\n');
            }
            if (newest > seen) write(seenF, String.valueOf(newest));
            return sb.length() > 0 ? sb.toString() : null;
        } catch (Throwable e) { return null; }
    }

    /** start screen button: the current log, any time */
    static void saveNow(final Context c) {
        new Thread(() -> {
            String name = "viceloader_log_" + new SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(new Date()) + ".txt";
            String text = header(c, "log saved by the user") + "overlay running: " + (OverlayService.sInstance != null) + "\n\n" + logcat(2000);
            String msg;
            try { save(c, name, text); msg = "Saved to Download/ViceLoader/" + name; }
            catch (Throwable e) { msg = "Saving the log failed: " + e.getMessage(); }
            final String m = msg;
            new Handler(Looper.getMainLooper()).post(() -> Toast.makeText(c, m, Toast.LENGTH_LONG).show());
        }, "vl-savelog").start();
    }

    private static void save(Context c, String name, String text) throws Exception {
        try (OutputStream o = Bundle.open(c, "", name, "text/plain")) { o.write(text.getBytes("UTF-8")); }
    }
    private static void write(File f, String s) {
        try (FileOutputStream o = new FileOutputStream(f)) { o.write(s.getBytes("UTF-8")); } catch (Throwable ignored) {}
    }
    private static String read(File f) {
        try (FileInputStream in = new FileInputStream(f)) { return new String(readAll(in), "UTF-8"); } catch (Throwable e) { return ""; }
    }
    private static byte[] readAll(InputStream in) throws Exception {
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        byte[] buf = new byte[8192];
        for (int n; (n = in.read(buf)) > 0; ) b.write(buf, 0, n);
        return b.toByteArray();
    }
}
