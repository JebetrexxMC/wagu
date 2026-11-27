package com.christe.mp;

import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.net.ConnectivityManager;
import android.net.NetworkInfo;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.view.View;
import android.view.WindowManager;
import android.webkit.JavascriptInterface;
import android.webkit.WebChromeClient;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Toast;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.FileWriter;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.HttpURLConnection;
import java.net.InetAddress;
import java.net.URL;
import java.util.Enumeration;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

// Christe Multiplayer 1.0 (com.christe.mp): a landscape WebView launcher (assets/www) for MP on
// GTA SA (AML, com.rockstargames.gtasa) and Vice City (com.rockstargames.gtavc / ViceLoader).
//  - the games are the ones already installed; they keep reading their own OBB
//    (Android/obb/com.rockstargames.gtasa/main.8 + patch.8), nothing is copied or repacked
//  - the server you pick goes to the game as Intent extras: libMP 5.02.76+ joins it from the title
//    (christe_join = "mp|name|port|host" or "omp|host|port|name", christe_nick = your name)
//  - Vice City: ViceLoader's local bridge (UDP 127.0.0.1:47790 "JOIN <port> <host>\t<pass>")
//  - its own files in Android/data/com.christe.mp/date2 (resources, libs, files, configs, mods, scripts)
//  - one app for both CPUs: arm64-v8a (GTA SA 2.10) or armeabi-v7a (GTA SA 2.00) picked from the device + the game
public class MainActivity extends Activity {
    static final String SA = "com.rockstargames.gtasa", VC = "com.rockstargames.gtavc", VL = "com.thirtyfoxmc.viceloader", MOQ = "com.thirtyfox.moquil";
    static final String[] DATE2_DIRS = { "resources", "libs", "files", "configs", "mods", "scripts", "logs" };
    WebView web;
    SharedPreferences prefs;
    BroadcastReceiver netRx;
    boolean lastOnline = true;

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        prefs = getSharedPreferences("christe", MODE_PRIVATE);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (Build.VERSION.SDK_INT >= 28) {
            WindowManager.LayoutParams lp = getWindow().getAttributes();
            lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(lp);
        }
        immersive();
        web = new WebView(this);
        web.setBackgroundColor(Color.rgb(7, 8, 12));
        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setDatabaseEnabled(true);
        s.setAllowFileAccess(true);
        s.setAllowFileAccessFromFileURLs(true);
        s.setMediaPlaybackRequiresUserGesture(false);
        s.setUserAgentString(s.getUserAgentString() + " ChristeMP/1.0");
        web.addJavascriptInterface(new Bridge(), "Christe");
        web.setWebViewClient(new WebViewClient() {
            @Override public boolean shouldOverrideUrlLoading(WebView v, String url) {
                if (url.startsWith("file:///android_asset/")) return false;
                openLink(url); return true;
            }
        });
        web.setWebChromeClient(new WebChromeClient());
        setContentView(web);
        new Thread(new Runnable() { public void run() { setupDate2(); } }).start();
        if (b != null) web.restoreState(b);
        else web.loadUrl("file:///android_asset/www/index.html");
        netRx = new BroadcastReceiver() {
            @Override public void onReceive(Context c, Intent i) {
                boolean on = isOnline();
                if (on != lastOnline) { lastOnline = on; js("window.CX_net && CX_net(" + on + ")"); }
            }
        };
        registerReceiver(netRx, new IntentFilter(ConnectivityManager.CONNECTIVITY_ACTION));
    }

    void immersive() {
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }
    @Override public void onWindowFocusChanged(boolean f) { super.onWindowFocusChanged(f); if (f) immersive(); }
    @Override protected void onResume() { super.onResume(); js("window.CX_resume && CX_resume()"); }
    @Override protected void onDestroy() { try { unregisterReceiver(netRx); } catch (Exception e) {} super.onDestroy(); }
    @Override protected void onSaveInstanceState(Bundle b) { super.onSaveInstanceState(b); web.saveState(b); }
    @Override public void onBackPressed() { web.evaluateJavascript("window.CX_back ? CX_back() : false", null); }

    void js(final String code) { runOnUiThread(new Runnable() { public void run() { if (web != null) web.evaluateJavascript(code, null); } }); }
    static String q(String s) {
        if (s == null) s = "";
        StringBuilder o = new StringBuilder("'");
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (c == '\\' || c == '\'') o.append('\\').append(c);
            else if (c == '\n') o.append("\\n");
            else if (c == '\r') o.append("\\r");
            else if (c == 0x2028 || c == 0x2029 || c < 0x20) o.append(String.format("\\u%04x", (int) c));
            else o.append(c);
        }
        return o.append("'").toString();
    }
    void openLink(String url) {
        try { startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url)).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)); }
        catch (Exception e) { toast("No app can open this link"); }
    }
    void toast(final String t) { runOnUiThread(new Runnable() { public void run() { Toast.makeText(MainActivity.this, t, Toast.LENGTH_SHORT).show(); } }); }

    boolean isOnline() {
        try {
            ConnectivityManager cm = (ConnectivityManager) getSystemService(CONNECTIVITY_SERVICE);
            NetworkInfo n = cm.getActiveNetworkInfo();
            return n != null && n.isConnected();
        } catch (Exception e) { return true; }
    }

    // ---- the files --------------------------------------------------------------------------
    File date2() {
        File f = getExternalFilesDir(null);              // Android/data/com.christe.mp/files
        File root = f != null ? f.getParentFile() : getFilesDir();
        return new File(root, "date2");                  // Android/data/com.christe.mp/date2
    }
    // 64-bit game (GTA SA 2.10 arm64-v8a) or 32-bit (GTA SA 2.00 armeabi-v7a)
    String gameAbi(String pkg) {
        try {
            ApplicationInfo ai = getPackageManager().getApplicationInfo(pkg, 0);
            String dir = ai.nativeLibraryDir != null ? ai.nativeLibraryDir : "";
            try {
                String p = (String) ApplicationInfo.class.getField("primaryCpuAbi").get(ai);
                if (p != null && p.length() > 0) return p;
            } catch (Throwable t) {}
            if (dir.endsWith("arm64") || dir.contains("arm64")) return "arm64-v8a";
            if (dir.endsWith("/arm") || dir.contains("armeabi")) return "armeabi-v7a";
        } catch (Exception e) {}
        return "";
    }
    String deviceAbi() {
        if (Build.VERSION.SDK_INT >= 21 && Build.SUPPORTED_64_BIT_ABIS != null && Build.SUPPORTED_64_BIT_ABIS.length > 0) return "arm64-v8a";
        return "armeabi-v7a";
    }
    String pickAbi() {
        String force = prefs.getString("abi", "auto");
        if ("arm64-v8a".equals(force) || "armeabi-v7a".equals(force)) return force;
        String g = gameAbi(SA);
        if (g.startsWith("arm64")) return "arm64-v8a";
        if (g.startsWith("armeabi")) return "armeabi-v7a";
        return deviceAbi();
    }
    JSONObject pkgInfo(String pkg) throws Exception {
        JSONObject o = new JSONObject();
        try {
            PackageInfo pi = getPackageManager().getPackageInfo(pkg, 0);
            o.put("installed", true); o.put("version", pi.versionName == null ? "" : pi.versionName); o.put("abi", gameAbi(pkg));
        } catch (PackageManager.NameNotFoundException e) { o.put("installed", false); }
        return o;
    }
    // the OBB files of a game: Android/obb (the normal place) and Android_unprotected (some phones / file managers)
    JSONArray obbCheck(String pkg, String[] names) throws Exception {
        String sd = Environment.getExternalStorageDirectory().getAbsolutePath();
        String[] dirs = { sd + "/Android/obb/" + pkg, sd + "/Android_unprotected/obb/" + pkg, sd + "/Android_unprotected/data/" + pkg + "/obb", sd + "/Android/data/" + pkg + "/obb" };
        JSONArray a = new JSONArray();
        for (String n : names) {
            JSONObject o = new JSONObject(); o.put("name", n); o.put("found", false); o.put("visible", false);
            for (String d : dirs) {
                File dir = new File(d), f = new File(d, n);
                if (dir.canRead()) o.put("visible", true);
                if (f.isFile()) { o.put("found", true); o.put("path", f.getAbsolutePath()); o.put("size", f.length()); break; }
            }
            a.put(o);
        }
        return a;
    }
    File ownObb() {
        File d = getObbDir();
        if (d == null) return null;
        d.mkdirs();
        File best = null;
        File[] l = d.listFiles();
        if (l != null) for (File f : l) if (f.getName().startsWith("main.") && f.getName().endsWith(".com.christe.mp.obb") && (best == null || f.lastModified() > best.lastModified())) best = f;
        return best;
    }
    synchronized JSONObject setupDate2() {
        JSONObject r = new JSONObject();
        try {
            File d = date2();
            for (String n : DATE2_DIRS) new File(d, n).mkdirs();
            File readme = new File(d, "README.txt");
            if (!readme.exists()) write(readme, "Christe Multiplayer - date2\n\nChriste Multiplayer's own folder, apart from GTA SA's Android/data/com.rockstargames.gtasa,\nso the two never clash.\n\n resources/  launcher pictures, sounds, startup.mp4 (your own boot video)\n libs/       libMP.so for this phone (arm64-v8a = GTA SA 2.10, armeabi-v7a = GTA SA 2.00)\n files/      downloads\n configs/    christe.ini\n mods/       mods to copy into GTA SA's AML mods folder\n scripts/    Lua / CLEO scripts\n logs/       launcher log\n\nThe game itself keeps its OBB in Android/obb/com.rockstargames.gtasa (main.8 + patch.8).\nChriste Multiplayer's own OBB (optional): Android/obb/com.christe.mp/main.<n>.com.christe.mp.obb (a zip, unpacked into resources/).\n");
            String abi = pickAbi();
            String lib = abi.startsWith("arm64") ? "libMP64.so" : "libMP32.so";
            File out = new File(d, "libs/" + abi + "/libMP.so");
            long have = prefs.getLong("lib_" + abi, 0);
            int ver = 0; try { ver = getPackageManager().getPackageInfo(getPackageName(), 0).versionCode; } catch (Exception e) {}
            if (!out.isFile() || have != ver) {
                try {
                    out.getParentFile().mkdirs();
                    copy(getAssets().open("libs/" + lib), new FileOutputStream(out));
                    prefs.edit().putLong("lib_" + abi, ver).apply();
                    r.put("lib", "installed");
                } catch (Exception e) { r.put("lib", "not in this APK"); }
            } else r.put("lib", "ok");
            r.put("libPath", out.getAbsolutePath());
            File obb = ownObb();
            if (obb != null) {
                String key = obb.getName() + ":" + obb.length() + ":" + obb.lastModified();
                if (!key.equals(prefs.getString("obb_done", ""))) {
                    int n = unzip(obb, new File(d, "resources"));
                    prefs.edit().putString("obb_done", key).apply();
                    r.put("obbUnpacked", n);
                }
                r.put("obb", obb.getAbsolutePath());
            }
            writeIni();
            r.put("ok", true); r.put("path", d.getAbsolutePath());
        } catch (Exception e) { try { r.put("ok", false); r.put("err", String.valueOf(e)); } catch (Exception x) {} }
        return r;
    }
    void writeIni() {
        try {
            File ini = new File(date2(), "configs/christe.ini");
            write(ini, "[Christe]\nname=" + prefs.getString("nick", "") + "\nabi=" + pickAbi() + "\ndevice_abi=" + deviceAbi() + "\nsa_package=" + prefs.getString("sa_pkg", SA) + "\nregion=" + prefs.getString("region", "auto") + "\n");
        } catch (Exception e) {}
    }
    static void write(File f, String t) throws Exception { f.getParentFile().mkdirs(); FileWriter w = new FileWriter(f); w.write(t); w.close(); }
    static void copy(InputStream in, OutputStream out) throws Exception {
        byte[] b = new byte[65536]; int n;
        try { while ((n = in.read(b)) > 0) out.write(b, 0, n); } finally { in.close(); out.close(); }
    }
    static int unzip(File zip, File to) {
        int n = 0;
        try {
            ZipFile z = new ZipFile(zip);
            String base = to.getCanonicalPath();
            for (Enumeration<? extends ZipEntry> e = z.entries(); e.hasMoreElements();) {
                ZipEntry en = e.nextElement();
                File f = new File(to, en.getName());
                if (!f.getCanonicalPath().startsWith(base)) continue;   // no ../ out of resources/
                if (en.isDirectory()) { f.mkdirs(); continue; }
                f.getParentFile().mkdirs();
                copy(z.getInputStream(en), new FileOutputStream(f)); n++;
            }
            z.close();
        } catch (Exception e) {}
        return n;
    }

    // ---- start a game -------------------------------------------------------------------------
    String launch(String pkg, String join, String nick) {
        Intent i = getPackageManager().getLaunchIntentForPackage(pkg);
        if (i == null) return "missing";
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        if (join != null && join.length() > 0) i.putExtra("christe_join", join);
        if (nick != null && nick.length() > 0) i.putExtra("christe_nick", nick);
        i.putExtra("christe_abi", pickAbi());
        i.putExtra("christe_date2", date2().getAbsolutePath());
        try { startActivity(i); return "ok"; } catch (Exception e) { return "error: " + e.getMessage(); }
    }
    static void udp(String host, int port, String msg) {
        try {
            DatagramSocket s = new DatagramSocket();
            byte[] b = msg.getBytes("UTF-8");
            s.send(new DatagramPacket(b, b.length, InetAddress.getByName(host), port));
            s.close();
        } catch (Exception e) {}
    }

    class Bridge {
        @JavascriptInterface public String info() {
            try {
                JSONObject o = new JSONObject();
                o.put("deviceAbi", deviceAbi()); o.put("abi", pickAbi()); o.put("abiMode", prefs.getString("abi", "auto"));
                JSONArray abis = new JSONArray(); if (Build.SUPPORTED_ABIS != null) for (String a : Build.SUPPORTED_ABIS) abis.put(a); o.put("abis", abis);
                o.put("sdk", Build.VERSION.SDK_INT); o.put("android", Build.VERSION.RELEASE); o.put("model", Build.MANUFACTURER + " " + Build.MODEL);
                String saPkg = prefs.getString("sa_pkg", SA);
                o.put("saPkg", saPkg); o.put("sa", pkgInfo(saPkg)); o.put("vc", pkgInfo(VC)); o.put("vl", pkgInfo(VL)); o.put("moquil", pkgInfo(MOQ));
                o.put("saObb", obbCheck(SA, new String[] { "main.8.com.rockstargames.gtasa.obb", "patch.8.com.rockstargames.gtasa.obb" }));
                File vcDir = new File(Environment.getExternalStorageDirectory(), "Android/obb/" + VC);
                String vcMain = "main.0.com.rockstargames.gtavc.obb";
                File[] vl = vcDir.listFiles(); if (vl != null) for (File f : vl) if (f.getName().startsWith("main.")) vcMain = f.getName();
                o.put("vcObb", obbCheck(VC, new String[] { vcMain }));
                File own = ownObb(); o.put("ownObb", own == null ? "" : own.getAbsolutePath()); o.put("ownObbDir", getObbDir() == null ? "" : getObbDir().getAbsolutePath());
                o.put("date2", date2().getAbsolutePath()); o.put("online", isOnline());
                o.put("version", getPackageManager().getPackageInfo(getPackageName(), 0).versionName);
                o.put("hasLib32", has("libs/libMP32.so")); o.put("hasLib64", has("libs/libMP64.so"));
                return o.toString();
            } catch (Exception e) { return "{}"; }
        }
        boolean has(String a) { try { getAssets().open(a).close(); return true; } catch (Exception e) { return false; } }
        @JavascriptInterface public boolean online() { return isOnline(); }
        @JavascriptInterface public String setup() { return setupDate2().toString(); }
        @JavascriptInterface public void set(String k, String v) {
            prefs.edit().putString(k, v).apply();
            if ("abi".equals(k)) new Thread(new Runnable() { public void run() { setupDate2(); } }).start(); else writeIni();
        }
        @JavascriptInterface public String startupVideo() {
            File f = new File(date2(), "resources/startup.mp4");
            return f.isFile() && f.length() > 1000 ? Uri.fromFile(f).toString() : "";
        }
        // GET in Java: no CORS for api.open.mp and the GitHub files
        @JavascriptInterface public void get(final String id, final String url) {
            new Thread(new Runnable() { public void run() {
                int code = 0; String body = "";
                try {
                    HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
                    c.setConnectTimeout(8000); c.setReadTimeout(12000);
                    c.setRequestProperty("User-Agent", "ChristeMP/1.0 (Android)");
                    c.setRequestProperty("Accept", "application/json, text/plain, */*");
                    code = c.getResponseCode();
                    InputStream in = code < 400 ? c.getInputStream() : c.getErrorStream();
                    if (in != null) {
                        ByteArrayOutputStream bo = new ByteArrayOutputStream(); byte[] b = new byte[16384]; int n;
                        while ((n = in.read(b)) > 0 && bo.size() < 4 * 1024 * 1024) bo.write(b, 0, n);
                        in.close(); body = bo.toString("UTF-8");
                    }
                    c.disconnect();
                } catch (Exception e) { code = 0; body = String.valueOf(e.getMessage()); }
                js("window.CX_got && CX_got(" + q(id) + "," + code + "," + q(body) + ")");
            }}).start();
        }
        // SA-MP / open.mp query ping ('p' packet), -1 = no answer
        @JavascriptInterface public void ping(final String id, final String host, final int port) {
            new Thread(new Runnable() { public void run() {
                int ms = -1;
                try {
                    InetAddress a = InetAddress.getByName(host);
                    byte[] ip = a.getAddress();
                    byte[] p = new byte[15];
                    p[0] = 'S'; p[1] = 'A'; p[2] = 'M'; p[3] = 'P';
                    if (ip.length == 4) { p[4] = ip[0]; p[5] = ip[1]; p[6] = ip[2]; p[7] = ip[3]; }
                    p[8] = (byte) (port & 255); p[9] = (byte) (port >> 8); p[10] = 'p';
                    p[11] = 1; p[12] = 2; p[13] = 3; p[14] = 4;
                    DatagramSocket s = new DatagramSocket(); s.setSoTimeout(1500);
                    long t0 = System.nanoTime();
                    s.send(new DatagramPacket(p, p.length, a, port));
                    byte[] r = new byte[64]; DatagramPacket rp = new DatagramPacket(r, r.length);
                    s.receive(rp);
                    ms = (int) ((System.nanoTime() - t0) / 1000000L);
                    s.close();
                } catch (Exception e) {}
                js("window.CX_ping && CX_ping(" + q(id) + "," + ms + ")");
            }}).start();
        }
        // game: "sa" | "vc"; join: "mp|name|port|host" / "omp|host|port|name" / ""
        @JavascriptInterface public String play(String game, String join, String nick, String pass) {
            prefs.edit().putString("nick", nick == null ? "" : nick).apply(); writeIni();
            if ("vc".equals(game)) {
                String r = launch(VL, "", nick);
                if (!"ok".equals(r)) r = launch(VC, join, nick);
                if ("ok".equals(r) && join != null && join.length() > 0) {
                    final String[] f = join.split("\\|", -1);
                    if (f.length >= 3) {
                        final String host = "omp".equals(f[0]) ? f[1] : (f.length > 3 ? f[3] : "");
                        final String port = f[2]; final String pw = pass == null ? "" : pass;
                        new Thread(new Runnable() { public void run() {
                            for (int i = 0; i < 6; i++) {   // ViceLoader's overlay comes up a few seconds after the game
                                try { Thread.sleep(2500); } catch (Exception e) {}
                                udp("127.0.0.1", 47790, "JOIN " + port + " " + host + "\t" + pw);
                            }
                        }}).start();
                    }
                }
                return r;
            }
            return launch(prefs.getString("sa_pkg", SA), join, nick);
        }
        @JavascriptInterface public boolean openApp(String pkg) { return "ok".equals(launch(pkg, "", "")); }
        @JavascriptInterface public void open(String url) { openLink(url); }
        @JavascriptInterface public void toast(String t) { MainActivity.this.toast(t); }
        @JavascriptInterface public void exit() { runOnUiThread(new Runnable() { public void run() { moveTaskToBack(true); } }); }
        @JavascriptInterface public void log(String t) {
            try { FileWriter w = new FileWriter(new File(date2(), "logs/christe_log.txt"), true); w.write(t + "\n"); w.close(); } catch (Exception e) {}
        }
    }
}
