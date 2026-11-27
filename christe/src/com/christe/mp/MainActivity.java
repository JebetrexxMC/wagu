package com.christe.mp;

import android.app.Activity;
import android.app.ActivityManager;
import android.database.Cursor;
import android.provider.OpenableColumns;
import android.provider.Settings;
import java.io.FileInputStream;
import java.util.zip.ZipInputStream;
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
import android.app.PendingIntent;
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

// AML Multiplayer (was Christe Multiplayer; package com.christe.mp): a landscape WebView launcher (assets/www) for MP on
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

    @Override protected void onNewIntent(Intent i) { super.onNewIntent(i); installStatus(i); }   // 1.4: edition switch
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
        s.setUserAgentString(s.getUserAgentString() + " AMLMultiplayer/1.3");
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
        if (l != null) for (File f : l) if (f.getName().startsWith("main.") && f.getName().endsWith(".com.christe.mp.obb") && !f.getName().contains(".8.") && f.length() < (100L << 20) && (best == null || f.lastModified() > best.lastModified())) best = f;
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
                    copy(mpLib(lib.equals("libMP64.so")), new FileOutputStream(out));
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
                    c.setRequestProperty("User-Agent", "AMLMultiplayer/1.3 (Android)");
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
            String pkg = prefs.getString("sa_pkg", SA);
            { String need = editionNeed(pkg); if (need.length() > 0) return "edition:" + need + ":" + editionText(need); }
            if (join != null && join.startsWith("coop|")) {   // Co-op runs in-app: libMP joins the CoopFusion32 server (1.3.1)
                String why = inAppProblem(pkg); if (why.length() > 0) return "noinapp:" + why;
                return launchInApp(pkg, join, nick);
            }
            if (!"app".equals(prefs.getString("engine", "christe"))) {
                String why = inAppProblem(pkg);
                if (why.length() == 0) return launchInApp(pkg, join, nick);
                log("in-app GTA SA not possible: " + why);
                return "noinapp:" + why;   // 1.2: never a silent switch to the GTA SA app (the launcher asks)
            }
            return launch(pkg, join, nick);
        }
        @JavascriptInterface public String obb() { return obbStatus().toString(); }
        @JavascriptInterface public void obbCopy(final String id) { new Thread(new Runnable() { public void run() { copyGameObb(id); } }).start(); }
        @JavascriptInterface public void obbImport(String id) { importId = id; pickObb(); }
        @JavascriptInterface public void obbAccess() { askObbAccess(); }
        @JavascriptInterface public void gameAdd(String id) { importId = id; pickApk(); }
        @JavascriptInterface public void editionSwitch(String id, String to) { switchEdition(id, "32".equals(to) ? "32" : "64"); }
        @JavascriptInterface public void gameRemove() { File f = importedApk(); f.setWritable(true); f.delete(); prefs.edit().putString("gamelib_stamp", "").apply(); }
        @JavascriptInterface public void mpFiles(final String id) { new Thread(new Runnable() { public void run() { downloadMpFiles(id); } }).start(); }
        @JavascriptInterface public String engine() {
            try {
                JSONObject o = new JSONObject();
                String pkg = prefs.getString("sa_pkg", SA);
                o.put("mode", prefs.getString("engine", "christe")); o.put("problem", inAppProblem(pkg));
                o.put("proc64", proc64()); o.put("mpFiles", new File(gameData(), "files/TYFMC/MP").isDirectory());
                o.put("mods", new File(gameData(), "mods").getAbsolutePath());
                File c = new File(date2(), "logs/game_crash.txt");
                if (c.isFile() && c.lastModified() > prefs.getLong("crash_seen", 0)) { o.put("crash", readHead(c, 1200)); }
                return o.toString();
            } catch (Exception e) { return "{}"; }
        }
        @JavascriptInterface public void crashSeen() { prefs.edit().putLong("crash_seen", System.currentTimeMillis()).apply(); }
        // 1.3 Co-op (CoopFusion32): server info over TCP ("INFO" -> INFO|name|players|max|version), LAN discovery over UDP
        @JavascriptInterface public void coopInfo(final String id, final String host, final int port) { new Thread(new Runnable() { public void run() { js("window.CX_coop && CX_coop(" + q(id) + "," + coopQuery(host, port).toString() + ")"); } }).start(); }
        @JavascriptInterface public void coopLan(final String id) { new Thread(new Runnable() { public void run() { js("window.CX_coopLan && CX_coopLan(" + q(id) + "," + coopDiscover().toString() + ")"); } }).start(); }
        @JavascriptInterface public String coopReady() { return coopProblem(); }
        @JavascriptInterface public void lan(final String id) { new Thread(new Runnable() { public void run() { lanScan(id); } }).start(); }
        @JavascriptInterface public String playApp(String join, String nick) { return launch(prefs.getString("sa_pkg", SA), join, nick); }
        @JavascriptInterface public boolean openApp(String pkg) { return "ok".equals(launch(pkg, "", "")); }
        @JavascriptInterface public void open(String url) { openLink(url); }
        @JavascriptInterface public void toast(String t) { MainActivity.this.toast(t); }
        @JavascriptInterface public void exit() { runOnUiThread(new Runnable() { public void run() { moveTaskToBack(true); } }); }
        @JavascriptInterface public void log(String t) { MainActivity.this.log(t); }
    }

    void log(String t) {
        try { FileWriter w = new FileWriter(new File(date2(), "logs/christe_log.txt"), true); w.write(t + "\n"); w.close(); } catch (Exception e) {}
    }
    static String readHead(File f, int max) {
        try { FileInputStream in = new FileInputStream(f); byte[] b = new byte[max]; int n = in.read(b); in.close(); return n > 0 ? new String(b, 0, n, "UTF-8") : ""; } catch (Exception e) { return ""; }
    }

    // ---- 1.1: GTA SA inside Christe (Game, process ":game") ---------------------------------
    File gameData() { File f = getExternalFilesDir(null); return f != null ? f.getParentFile() : getFilesDir(); }   // Android/data/com.christe.mp
    static boolean proc64() {
        if (Build.VERSION.SDK_INT >= 23) return android.os.Process.is64Bit();
        String a = System.getProperty("os.arch", ""); return a.contains("64");
    }
    // ---- 1.4: editions. Android fixes one CPU mode per app: the normal AML Multiplayer runs 64-bit on a 64-bit phone
    // (GTA SA 2.10, arm64-v8a), AML Multiplayer 32-bit (same app, lib/armeabi-v7a inside) runs 32-bit (GTA SA 2.00).
    // A GTA SA with no arm64-v8a is still added; the app then offers to switch to the 32-bit edition (and back).
    static final String ED_URL = "https://github.com/JebetrexxMC/wagu/releases/download/aml-multiplayer-%s/%s";
    static boolean has32() { return Build.VERSION.SDK_INT >= 21 && Build.SUPPORTED_32_BIT_ABIS != null && Build.SUPPORTED_32_BIT_ABIS.length > 0; }
    static boolean has64() { return Build.VERSION.SDK_INT >= 21 && Build.SUPPORTED_64_BIT_ABIS != null && Build.SUPPORTED_64_BIT_ABIS.length > 0; }
    static String procAbi() { return proc64() ? "arm64-v8a" : "armeabi-v7a"; }
    String edition() { return proc64() ? "64" : (has64() ? "32" : "32only"); }   // 32only = a 32-bit phone (one edition fits)
    // which game CPUs a GTA SA has: "64", "32", "both" or ""
    static String apkAbis(java.util.List<String> apks) {
        boolean a64 = false, a32 = false;
        for (String a : apks) { a64 |= zipHas(a, "lib/arm64-v8a/libGTASA.so"); a32 |= zipHas(a, "lib/armeabi-v7a/libGTASA.so"); }
        return a64 && a32 ? "both" : a64 ? "64" : a32 ? "32" : "";
    }
    // "" = this edition runs the added GTA SA, "32" / "64" = switch to that edition, "no32" / "no64" = this phone can't
    String editionNeed(String pkg) {
        ApplicationInfo ai = gameAi(pkg); if (ai == null) return "";
        String g = apkAbis(apks(ai));
        if (g.length() == 0 && !isImported(ai)) { String a = gameAbi(pkg); g = a.startsWith("arm64") ? "64" : a.startsWith("armeabi") ? "32" : ""; }
        if (g.length() == 0 || "both".equals(g) || g.equals(proc64() ? "64" : "32")) return "";
        if ("32".equals(g)) return has32() ? "32" : "no32";
        return has64() ? "64" : "no64";
    }
    static String editionText(String need) {
        if ("32".equals(need)) return "This GTA SA is 32-bit only (no arm64-v8a, GTA SA 2.00). Switch to AML Multiplayer 32-bit to play it. Your settings, game files and the added GTA SA stay.";
        if ("64".equals(need)) return "This GTA SA is 64-bit only (arm64-v8a, GTA SA 2.10). Switch back to the normal AML Multiplayer (64-bit) to play it.";
        if ("no32".equals(need)) return "This GTA SA is 32-bit only, and this phone can't run 32-bit apps at all. Add GTA SA 2.10 (64-bit, arm64-v8a).";
        if ("no64".equals(need)) return "This GTA SA is 64-bit only, and this phone is 32-bit. Add GTA SA 2.00 (32-bit, armeabi-v7a).";
        return "";
    }
    // downloads the other edition from the AML Multiplayer release and installs it over this one (same app, data kept)
    void switchEdition(final String id, final String to) {
        new Thread(new Runnable() { public void run() {
            String file = "32".equals(to) ? "AMLMultiplayer-32bit.apk" : "AMLMultiplayer.apk";
            String ver = ""; try { ver = getPackageManager().getPackageInfo(getPackageName(), 0).versionName.replaceAll("\\.0$", ""); } catch (Exception e) {}   // 1.4.0 -> release aml-multiplayer-1.4
            File apk = new File(getExternalCacheDir() != null ? getExternalCacheDir() : getCacheDir(), file), tmp = new File(apk.getPath() + ".part");
            long done = 0, total = 0;
            try {
                String[] urls = { String.format(ED_URL, ver, file), "https://github.com/JebetrexxMC/wagu/releases/latest/download/" + file };
                HttpURLConnection c = null;
                for (String u : urls) {
                    c = (HttpURLConnection) new URL(u).openConnection(); c.setInstanceFollowRedirects(true); c.setConnectTimeout(15000); c.setReadTimeout(30000);
                    if (c.getResponseCode() == 200) break; c.disconnect(); c = null;
                }
                if (c == null) { progress(id, "error", 0, 0, "The " + ("32".equals(to) ? "32-bit" : "64-bit") + " edition isn't online yet"); return; }
                total = c.getContentLength();
                InputStream in = c.getInputStream(); OutputStream out = new FileOutputStream(tmp);
                byte[] b = new byte[1 << 16]; int n; long last = 0;
                while ((n = in.read(b)) > 0) { out.write(b, 0, n); done += n; if (done - last > (2 << 20)) { last = done; progress(id, "copy", done, total, "AML Multiplayer " + ("32".equals(to) ? "32-bit" : "64-bit")); } }
                in.close(); out.close(); apk.delete(); tmp.renameTo(apk);
                PackageInfo pi = getPackageManager().getPackageArchiveInfo(apk.getAbsolutePath(), 0);
                if (pi == null || !getPackageName().equals(pi.packageName)) { apk.delete(); progress(id, "error", 0, 0, "The download is not AML Multiplayer"); return; }
                prefs.edit().putString("edition_to", to).commit();
                progress(id, "done", done, done, "Installing AML Multiplayer " + ("32".equals(to) ? "32-bit" : "64-bit") + "...");
                installApk(apk);
            } catch (Exception e) { tmp.delete(); progress(id, "error", done, total, String.valueOf(e.getMessage())); }
        }}).start();
    }
    static final String ACT_INSTALLED = "com.christe.mp.INSTALL_STATUS";
    void installApk(File apk) throws Exception {
        if (Build.VERSION.SDK_INT >= 26 && !getPackageManager().canRequestPackageInstalls()) {
            toast("Allow AML Multiplayer to install apps, then tap Switch again");
            startActivity(new Intent("android.settings.MANAGE_UNKNOWN_APP_SOURCES", Uri.parse("package:" + getPackageName())));
            return;
        }
        android.content.pm.PackageInstaller pk = getPackageManager().getPackageInstaller();
        android.content.pm.PackageInstaller.SessionParams sp = new android.content.pm.PackageInstaller.SessionParams(android.content.pm.PackageInstaller.SessionParams.MODE_FULL_INSTALL);
        sp.setAppPackageName(getPackageName()); sp.setSize(apk.length());
        int sid = pk.createSession(sp);
        android.content.pm.PackageInstaller.Session se = pk.openSession(sid);
        InputStream in = new FileInputStream(apk); OutputStream out = se.openWrite("base.apk", 0, apk.length());
        byte[] b = new byte[1 << 16]; int n; while ((n = in.read(b)) > 0) out.write(b, 0, n);
        se.fsync(out); in.close(); out.close();
        Intent i = new Intent(this, MainActivity.class).setAction(ACT_INSTALLED).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
        int fl = PendingIntent.FLAG_UPDATE_CURRENT | (Build.VERSION.SDK_INT >= 31 ? (1 << 25) : 0);   // FLAG_MUTABLE: the installer adds its status
        se.commit(PendingIntent.getActivity(this, sid, i, fl).getIntentSender());
        se.close();
    }
    // the installer's answer (singleTask: onNewIntent)
    boolean installStatus(Intent i) {
        if (i == null || !ACT_INSTALLED.equals(i.getAction())) return false;
        int st = i.getIntExtra(android.content.pm.PackageInstaller.EXTRA_STATUS, -999);
        if (st == android.content.pm.PackageInstaller.STATUS_PENDING_USER_ACTION) {
            Intent c = (Intent) i.getParcelableExtra(Intent.EXTRA_INTENT); if (c != null) { c.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK); startActivity(c); }
        } else if (st != android.content.pm.PackageInstaller.STATUS_SUCCESS) {
            String m = i.getStringExtra(android.content.pm.PackageInstaller.EXTRA_STATUS_MESSAGE);
            toast("The switch didn't install" + (m != null ? ": " + m : ""));
            log("edition install failed: " + st + " " + m);
        }
        return true;
    }
    // the MP library for this process: assets/libs (normal edition) or lib/armeabi-v7a (32-bit edition)
    InputStream mpLib(boolean is64) throws Exception {
        String n = is64 ? "libMP64.so" : "libMP32.so";
        try { return getAssets().open("libs/" + n); } catch (Exception e) {}
        return new FileInputStream(new File(getApplicationInfo().nativeLibraryDir, n));
    }
    // "" = the installed GTA SA can run inside AML Multiplayer, else why not (1.2: only real blockers)
    String inAppProblem(String pkg) {
        ApplicationInfo ai = gameAi(pkg);
        if (ai == null) return "GTA SA is not added yet (Add GTA SA in Storage, or install the AML GTA SA)";
        String abi = proc64() ? "arm64-v8a" : "armeabi-v7a";
        if (new File(gameLibDir(abi), "libGTASA.so").isFile()) return "";
        if (!isImported(ai) && ai.nativeLibraryDir != null && new File(ai.nativeLibraryDir, "libGTASA.so").isFile() && gameAbi(pkg).startsWith(proc64() ? "arm64" : "armeabi")) return "";
        for (String apk : apks(ai)) if (zipHas(apk, "lib/" + abi + "/libGTASA.so")) return "";
        String need = editionNeed(pkg); if (need.length() > 0) return editionText(need);
        return "this GTA SA has no " + abi + " game (" + (proc64() ? "this edition needs GTA SA 2.10, 64-bit" : "this edition needs GTA SA 2.00, 32-bit") + ")";
    }
    static java.util.List<String> apks(ApplicationInfo ai) {
        java.util.List<String> l = new java.util.ArrayList<String>(); l.add(ai.sourceDir);
        if (ai.splitSourceDirs != null) for (String x : ai.splitSourceDirs) l.add(x);
        return l;
    }
    // 1.3: "Add GTA SA" - the AML GTA SA APK picked by the player, copied into AML Multiplayer (read-only: Android 14
    // only loads code from read-only files). With it GTA SA does not have to be installed at all.
    File importedApk() { return new File(getDir("game", MODE_PRIVATE), "gtasa.apk"); }
    boolean isImported(ApplicationInfo ai) { return ai != null && importedApk().getAbsolutePath().equals(ai.sourceDir); }
    ApplicationInfo gameAi(String pkg) {
        File imp = importedApk();
        if (imp.isFile() && !"installed".equals(prefs.getString("game_src", ""))) {
            try {
                PackageInfo pi = getPackageManager().getPackageArchiveInfo(imp.getAbsolutePath(), 0);
                if (pi != null) { ApplicationInfo ai = pi.applicationInfo; ai.sourceDir = ai.publicSourceDir = imp.getAbsolutePath(); ai.splitSourceDirs = null; ai.nativeLibraryDir = null; return ai; }
            } catch (Exception e) {}
        }
        try { return getPackageManager().getApplicationInfo(pkg, 0); } catch (Exception e) {}
        return null;
    }
    // 1.3: the game files folder for the fastman92 settings, relative to Android/obb (see GameEnv)
    String obbDirFor() {
        String sd = Environment.getExternalStorageDirectory().getAbsolutePath(), o = "com.rockstargames.gtasa", g = prefs.getString("sa_pkg", SA);
        if (ownObbFile("main").isFile()) return getPackageName();
        if (new File(sd + "/Android_unprotected/obb/" + g, "main.8." + g + ".obb").isFile()) return "../../Android_unprotected/obb/" + g;
        if (new File(sd + "/Android/obb/" + g, "main.8." + g + ".obb").canRead()) return g;
        if (new File(sd + "/GTASA", "main.8." + o + ".obb").isFile()) return "../../GTASA";
        return getPackageName();
    }
    File gameLibDir(String abi) { return new File(getDir("gamelib", MODE_PRIVATE), abi); }
    // 1.2: the game's native libraries (libGTASA, libAML, libImmEmulatorJ, ...) for this CPU, unpacked from its APK
    // into AML Multiplayer's own folder: it works for split APKs, compressed libs and a game installed as the other ABI
    File unpackGameLibs(String pkg, String abi) {
        File dir = gameLibDir(abi);
        try {
            ApplicationInfo ai = gameAi(pkg); if (ai == null) return dir;
            File src = new File(ai.sourceDir);
            String stamp = ai.sourceDir + ":" + src.length() + ":" + src.lastModified() + ":" + abi;
            if (stamp.equals(prefs.getString("gamelib_stamp", "")) && new File(dir, "libGTASA.so").isFile()) return dir;
            dir.mkdirs(); for (File f : dir.listFiles()) f.delete();
            String pre = "lib/" + abi + "/"; int n = 0;
            for (String apk : apks(ai)) {
                ZipFile z = new ZipFile(apk);
                for (Enumeration<? extends ZipEntry> e = z.entries(); e.hasMoreElements();) {
                    ZipEntry en = e.nextElement(); String nm = en.getName();
                    if (!nm.startsWith(pre) || !nm.endsWith(".so") || nm.indexOf('/', pre.length()) >= 0) continue;
                    copy(z.getInputStream(en), new FileOutputStream(new File(dir, nm.substring(pre.length())))); n++;
                }
                z.close();
            }
            if (n == 0 && ai.nativeLibraryDir != null && new File(ai.nativeLibraryDir).isDirectory()) {   // libs only in the installed folder
                File[] fs = new File(ai.nativeLibraryDir).listFiles();
                if (fs != null) for (File f : fs) if (f.getName().endsWith(".so")) { copy(new FileInputStream(f), new FileOutputStream(new File(dir, f.getName()))); n++; }
            }
            for (File f : dir.listFiles()) { f.setReadable(true, false); f.setExecutable(true, false); }
            prefs.edit().putString("gamelib_stamp", stamp).apply();
            log("game libs unpacked: " + n + " for " + abi);
        } catch (Exception e) { log("game libs: " + e); }
        return dir;
    }
    // 1.2: the AML mods next to GTA SA (SAUtils, CamFX, ...) come along when they can be read; libMP is our own
    void copyGameMods(String pkg, File mods) {
        String sd = Environment.getExternalStorageDirectory().getAbsolutePath();
        String[] dirs = { sd + "/Android/data/" + pkg + "/mods", sd + "/Android_unprotected/data/" + pkg + "/mods" };
        for (String d : dirs) {
            File[] fs = new File(d).listFiles(); if (fs == null) continue;
            for (File f : fs) {
                String n = f.getName();
                if (!f.isFile() || !n.endsWith(".so") || n.equalsIgnoreCase("libMP.so")) continue;
                File to = new File(mods, n);
                if (to.isFile() && to.length() == f.length()) continue;
                try { copy(new FileInputStream(f), new FileOutputStream(to)); log("mod copied: " + n); } catch (Exception e) {}
            }
        }
    }
    static boolean zipHas(String zip, String name) { try { ZipFile z = new ZipFile(zip); boolean h = z.getEntry(name) != null; z.close(); return h; } catch (Exception e) { return false; } }
    static boolean dexHas(String apk, String cls) {
        try {
            byte[] needle = cls.getBytes("UTF-8");
            ZipFile z = new ZipFile(apk);
            for (Enumeration<? extends ZipEntry> e = z.entries(); e.hasMoreElements();) {
                ZipEntry en = e.nextElement(); String n = en.getName();
                if (!n.startsWith("classes") || !n.endsWith(".dex")) continue;
                InputStream in = z.getInputStream(en); byte[] buf = new byte[1 << 20]; int keep = 0, r;
                while ((r = in.read(buf, keep, buf.length - keep)) > 0) {
                    int len = keep + r;
                    outer: for (int i = 0; i + needle.length <= len; i++) { for (int j = 0; j < needle.length; j++) if (buf[i + j] != needle[j]) continue outer; in.close(); z.close(); return true; }
                    keep = Math.min(needle.length, len); System.arraycopy(buf, len - keep, buf, 0, keep);
                }
                in.close();
            }
            z.close();
        } catch (Exception e) {}
        return false;
    }
    String launchInApp(String pkg, String join, String nick) {
        try {
            ApplicationInfo ai = gameAi(pkg);
            if (ai == null) return "error: GTA SA is not added";
            String abi = proc64() ? "arm64-v8a" : "armeabi-v7a";
            File gl = unpackGameLibs(pkg, abi);
            migrateObb();
            GameEnv.save(this, pkg, ai, abi, gl.getAbsolutePath(), obbDirFor());
            // AML in Christe reads Android/data/com.christe.mp/mods: libMP for this CPU goes there
            File mods = new File(gameData(), "mods"); mods.mkdirs();
            File lib = new File(mods, "libMP.so");

            int ver = 0; try { ver = getPackageManager().getPackageInfo(getPackageName(), 0).versionCode; } catch (Exception e) {}
            File off = new File(mods, "libMP.so.coop");
            if (off.isFile() && !lib.isFile()) off.renameTo(lib);
            if (!lib.isFile() || prefs.getInt("modlib", 0) != ver || !procAbi().equals(prefs.getString("modlib_abi", ""))) { try { copy(mpLib(proc64()), new FileOutputStream(lib)); prefs.edit().putInt("modlib", ver).putString("modlib_abi", procAbi()).apply(); } catch (Exception e) { log("libMP copy: " + e); } }
            copyGameMods(pkg, mods);
            setupCoop(mods, join, nick);
            new File(gameData(), "files").mkdirs(); new File(gameData(), "configs").mkdirs();
            killGame();
            Intent i = new Intent().setClassName(getPackageName(), "com.christe.mp.Game");
            i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
            if (join != null && join.length() > 0) i.putExtra("christe_join", join);
            if (nick != null && nick.length() > 0) i.putExtra("christe_nick", nick);
            i.putExtra("christe_abi", proc64() ? "arm64-v8a" : "armeabi-v7a");
            i.putExtra("christe_date2", date2().getAbsolutePath());
            i.putExtra("christe_ingame", true);
            startActivity(i);
            log("in-app GTA SA started: " + pkg + " join=" + join);
            return "ok";
        } catch (Exception e) { log("in-app start failed: " + e); return "error: " + e.getMessage(); }
    }
    // ---- Co-op (CoopFusion32 servers): libMP joins them itself (crossplay, 1.3.1), on 32-bit and 64-bit GTA SA.
    // The collaborator's CoopFusion32 mod is not copied, changed or switched in; only 1.3 leftovers are cleaned up.
    String coopProblem() { return ""; }
    void setupCoop(File mods, String join, String nick) {
        File old = new File(mods, "libCoopFusion32.so"), mp = new File(mods, "libMP.so"), off = new File(mods, "libMP.so.coop");
        if (old.isFile() && prefs.getBoolean("coop13", true)) { old.delete(); prefs.edit().putBoolean("coop13", false).apply(); }   // the copy AML Multiplayer 1.3 put there
        if (off.isFile()) { if (!mp.isFile()) off.renameTo(mp); else off.delete(); }
        if (join != null && join.startsWith("coop|")) log("co-op session through libMP: " + join);
    }
    JSONObject coopQuery(String host, int port) {
        JSONObject o = new JSONObject();
        try {
            o.put("ok", false);
            long t0 = System.nanoTime();
            java.net.Socket so = new java.net.Socket(); so.connect(new java.net.InetSocketAddress(host, port), 2500); so.setSoTimeout(2500);
            so.getOutputStream().write("INFO\n".getBytes("UTF-8")); so.getOutputStream().flush();
            java.io.BufferedReader r = new java.io.BufferedReader(new java.io.InputStreamReader(so.getInputStream(), "UTF-8"));
            String line = null; so.setSoTimeout(700);
            try { line = r.readLine(); } catch (Exception e) {}   // a stock CoopFusion32 server answers nothing to INFO: being reachable is enough
            so.close();
            o.put("ms", (int) ((System.nanoTime() - t0) / 1000000L));
            if (line != null && line.startsWith("INFO|")) {
                String[] f = line.split("\\|", -1);
                o.put("ok", true); o.put("name", f.length > 1 ? f[1] : host);
                o.put("players", f.length > 2 ? parse(f[2]) : 0); o.put("max", f.length > 3 ? parse(f[3]) : 0); o.put("ver", f.length > 4 ? f[4] : "");
            } else { o.put("ok", true); o.put("name", host); o.put("ver", ""); }
        } catch (Exception e) { try { o.put("err", String.valueOf(e.getMessage())); } catch (Exception x) {} }
        return o;
    }
    static int parse(String v) { try { return Integer.parseInt(v.trim()); } catch (Exception e) { return 0; } }
    JSONArray coopDiscover() {
        JSONArray out = new JSONArray();
        try {
            DatagramSocket s = new DatagramSocket(); s.setBroadcast(true); s.setSoTimeout(200);
            byte[] q = "COOP_DISCOVER".getBytes("UTF-8");
            java.util.LinkedHashSet<String> hosts = new java.util.LinkedHashSet<String>();
            hosts.add("255.255.255.255"); hosts.add("192.168.43.1"); hosts.add("192.168.49.1"); hosts.add("172.20.10.1"); hosts.add("127.0.0.1");
            try {
                for (Enumeration<java.net.NetworkInterface> en = java.net.NetworkInterface.getNetworkInterfaces(); en.hasMoreElements();) {
                    java.net.NetworkInterface ni = en.nextElement();
                    if (!ni.isUp() || ni.isLoopback()) continue;
                    for (java.net.InterfaceAddress ia : ni.getInterfaceAddresses()) if (ia.getBroadcast() != null) hosts.add(ia.getBroadcast().getHostAddress());
                }
            } catch (Exception e) {}
            for (int port = 6768; port <= 6771; port++) for (String h : hosts) { try { s.send(new DatagramPacket(q, q.length, InetAddress.getByName(h), port)); } catch (Exception e) {} }
            java.util.HashSet<String> seen = new java.util.HashSet<String>();
            long end = System.currentTimeMillis() + 1600; byte[] b = new byte[512];
            while (System.currentTimeMillis() < end) {
                DatagramPacket p = new DatagramPacket(b, b.length);
                try { s.receive(p); } catch (Exception e) { continue; }
                String m = new String(b, 0, p.getLength(), "UTF-8").trim();
                if (!m.startsWith("COOP_HERE|")) continue;
                String[] f = m.split("\\|", -1); if (f.length < 3) continue;
                String host = p.getAddress().getHostAddress(); if (!seen.add(host + ":" + f[2])) continue;
                JSONObject o = new JSONObject();
                o.put("host", host); o.put("name", f[1]); o.put("port", parse(f[2]));
                o.put("players", f.length > 3 ? parse(f[3]) : 0); o.put("max", f.length > 4 ? parse(f[4]) : 0); o.put("ver", f.length > 5 ? f[5] : "");
                out.put(o);
            }
            s.close();
        } catch (Exception e) {}
        return out;
    }
    void killGame() {
        try {
            ActivityManager am = (ActivityManager) getSystemService(ACTIVITY_SERVICE);
            for (ActivityManager.RunningAppProcessInfo p : am.getRunningAppProcesses())
                if ((getPackageName() + ":game").equals(p.processName)) android.os.Process.killProcess(p.pid);
        } catch (Exception e) {}
    }

    // ---- 1.1: Christe's own OBB (Android/obb/com.christe.mp/main.8 + patch.8.com.christe.mp.obb) ----
    static final String[] OBB_KIND = { "main", "patch" };
    File gameObb(String kind) {
        String sd = Environment.getExternalStorageDirectory().getAbsolutePath(), g = prefs.getString("sa_pkg", SA), n = kind + ".8." + g + ".obb";
        String[] dirs = { sd + "/Android/obb/" + g, sd + "/Android_unprotected/obb/" + g, sd + "/Android_unprotected/data/" + g + "/obb" };
        for (String d : dirs) { File f = new File(d, n); if (f.isFile() && f.canRead()) return f; }
        File f = new File(sd + "/GTASA", kind + ".8.com.rockstargames.gtasa.obb");
        return f.isFile() && f.canRead() ? f : null;
    }
    File ownObbFile(String kind) { File d = getObbDir(); if (d == null) d = new File(Environment.getExternalStorageDirectory(), "Android/obb/" + getPackageName()); d.mkdirs(); return new File(d, kind + ".8.com.rockstargames.gtasa.obb"); }
    // 1.3: the AML loaders open <kind>.8.com.rockstargames.gtasa.obb; 1.1/1.2 saved <kind>.8.com.christe.mp.obb
    void migrateObb() {
        for (String k : OBB_KIND) {
            File n = ownObbFile(k), old = new File(n.getParentFile(), k + ".8.com.christe.mp.obb");
            if (old.isFile() && !n.isFile()) old.renameTo(n);
        }
    }
    JSONObject obbStatus() {
        JSONObject o = new JSONObject();
        try {
            migrateObb();
            for (String k : OBB_KIND) {
                JSONObject e = new JSONObject();
                File own = ownObbFile(k), g = gameObb(k);
                e.put("own", own.isFile() ? own.length() : 0); e.put("ownPath", own.getAbsolutePath());
                e.put("game", g != null ? g.length() : 0); e.put("gamePath", g != null ? g.getAbsolutePath() : "");
                o.put(k, e);
            }
            o.put("ready", ownObbFile("main").isFile() || gameObb("main") != null);
            o.put("canAsk", Build.VERSION.SDK_INT >= 23);
            ApplicationInfo ai = gameAi(prefs.getString("sa_pkg", SA));
            o.put("gameAdded", ai != null); o.put("gameImported", isImported(ai));
            if (ai != null) { File a = new File(ai.sourceDir); o.put("gameSize", a.length()); }
            o.put("gameNeed", proc64() ? "GTA SA 2.10 AML (64-bit, arm64-v8a)" + (has32() ? " - a 32-bit GTA SA 2.00 works with AML Multiplayer 32-bit" : "") : has64() ? "GTA SA 2.00 AML (32-bit, armeabi-v7a) - a 64-bit GTA SA 2.10 works with the normal AML Multiplayer" : "GTA SA 2.00 AML (32-bit)");
            String pk = prefs.getString("sa_pkg", SA); String need = editionNeed(pk);
            o.put("edition", edition()); o.put("has32", has32()); o.put("editionNeed", need); o.put("editionText", editionText(need));
            if (ai != null) { String g = apkAbis(apks(ai)); o.put("gameAbi", "both".equals(g) ? "32-bit + 64-bit" : "64".equals(g) ? "64-bit (arm64-v8a)" : "32".equals(g) ? "32-bit only (armeabi-v7a)" : ""); }
        } catch (Exception e) {}
        return o;
    }
    void progress(String id, String state, long done, long total, String msg) {
        js("window.CX_prog && CX_prog(" + q(id) + "," + q(state) + "," + done + "," + total + "," + q(msg) + ")");
    }
    void copyGameObb(String id) {
        long total = 0, done = 0;
        for (String k : OBB_KIND) { File g = gameObb(k); if (g != null) total += g.length(); }
        if (total == 0) { progress(id, "error", 0, 0, "The GTA SA game files can't be read from here. Allow access or import the files."); return; }
        try {
            for (String k : OBB_KIND) {
                File g = gameObb(k); if (g == null) continue;
                File o = ownObbFile(k), tmp = new File(o.getPath() + ".part");
                if (o.isFile() && o.length() == g.length()) { done += g.length(); continue; }
                InputStream in = new FileInputStream(g); OutputStream out = new FileOutputStream(tmp);
                byte[] b = new byte[1 << 20]; int n; long last = 0;
                while ((n = in.read(b)) > 0) { out.write(b, 0, n); done += n; if (done - last > (8 << 20)) { last = done; progress(id, "copy", done, total, k); } }
                in.close(); out.close();
                o.delete(); tmp.renameTo(o);
            }
            progress(id, "done", total, total, "Game files ready");
        } catch (Exception e) { progress(id, "error", done, total, String.valueOf(e.getMessage())); }
    }
    String importId = "";
    static final int REQ_OBB = 41, REQ_APK = 42;
    void pickApk() {
        runOnUiThread(new Runnable() { public void run() {
            try {
                Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*");
                startActivityForResult(i, REQ_APK);
            } catch (Exception e) { toast("No file picker on this phone"); }
        }});
    }
    void importApk(final Uri uri, final String id) {
        new Thread(new Runnable() { public void run() {
            File to = importedApk(), tmp = new File(to.getPath() + ".part");
            long total = 0, done = 0;
            try {
                try { Cursor c = getContentResolver().query(uri, null, null, null, null); if (c != null && c.moveToFirst()) total = c.getLong(c.getColumnIndex(OpenableColumns.SIZE)); if (c != null) c.close(); } catch (Exception e) {}
                InputStream in = getContentResolver().openInputStream(uri); OutputStream out = new FileOutputStream(tmp);
                byte[] b = new byte[1 << 20]; int r; long last = 0;
                while ((r = in.read(b)) > 0) { out.write(b, 0, r); done += r; if (done - last > (4 << 20)) { last = done; progress(id, "copy", done, total, "GTA SA"); } }
                in.close(); out.close();
                String abi = proc64() ? "arm64-v8a" : "armeabi-v7a";
                PackageInfo pi = getPackageManager().getPackageArchiveInfo(tmp.getAbsolutePath(), 0);
                if (pi == null) { tmp.delete(); progress(id, "error", 0, 0, "That file is not an APK"); return; }
                java.util.List<String> one = new java.util.ArrayList<String>(); one.add(tmp.getAbsolutePath());
                String g = apkAbis(one);
                if (g.length() == 0) { tmp.delete(); progress(id, "error", 0, 0, "This APK has no GTA SA game inside (no lib/arm64-v8a or lib/armeabi-v7a libGTASA.so)"); return; }
                if ("32".equals(g) && !has32()) { tmp.delete(); progress(id, "error", 0, 0, editionText("no32")); return; }
                if ("64".equals(g) && !has64()) { tmp.delete(); progress(id, "error", 0, 0, editionText("no64")); return; }
                if (!g.equals("both") && !g.equals(proc64() ? "64" : "32")) log("GTA SA added for the other edition: " + g + "-bit (no " + abi + ")");
                if (!dexHas(tmp.getAbsolutePath(), "com/fastman92/main_activity_launcher")) log("imported APK has no fastman92 launcher code: " + pi.packageName);
                if (to.isFile()) { to.setWritable(true); to.delete(); }
                tmp.renameTo(to); to.setReadOnly();
                prefs.edit().putString("game_src", "imported").putString("gamelib_stamp", "").apply();
                log("GTA SA added: " + pi.packageName + " " + pi.versionName + " " + to.length());
                progress(id, "done", done, done, "GTA SA " + (pi.versionName == null ? "" : pi.versionName) + " added");
            } catch (Exception e) { tmp.delete(); progress(id, "error", done, total, String.valueOf(e.getMessage())); }
        }}).start();
    }
    void pickObb() {
        runOnUiThread(new Runnable() { public void run() {
            try {
                Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*");
                i.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
                startActivityForResult(i, REQ_OBB);
            } catch (Exception e) { toast("No file picker on this phone"); }
        }});
    }
    @Override protected void onActivityResult(int req, int res, final Intent data) {
        super.onActivityResult(req, res, data);
        if (req == REQ_APK) {
            if (res != RESULT_OK || data == null || data.getData() == null) { progress(importId, "cancel", 0, 0, ""); return; }
            importApk(data.getData(), importId); return;
        }
        if (req != REQ_OBB) return;
        if (res != RESULT_OK || data == null) { progress(importId, "cancel", 0, 0, ""); return; }
        final java.util.ArrayList<Uri> uris = new java.util.ArrayList<Uri>();
        if (data.getClipData() != null) for (int i = 0; i < data.getClipData().getItemCount(); i++) uris.add(data.getClipData().getItemAt(i).getUri());
        else if (data.getData() != null) uris.add(data.getData());
        final String id = importId;
        new Thread(new Runnable() { public void run() {
            long total = 0, done = 0; int ok = 0;
            String[] names = new String[uris.size()]; long[] sizes = new long[uris.size()];
            for (int i = 0; i < uris.size(); i++) {
                names[i] = ""; sizes[i] = 0;
                try { Cursor c = getContentResolver().query(uris.get(i), null, null, null, null); if (c != null && c.moveToFirst()) { names[i] = c.getString(c.getColumnIndex(OpenableColumns.DISPLAY_NAME)); sizes[i] = c.getLong(c.getColumnIndex(OpenableColumns.SIZE)); } if (c != null) c.close(); } catch (Exception e) {}
                total += sizes[i];
            }
            try {
                for (int i = 0; i < uris.size(); i++) {
                    String n = names[i] == null ? "" : names[i].toLowerCase();
                    String kind = n.startsWith("patch") ? "patch" : n.startsWith("main") ? "main" : "";
                    if (kind.length() == 0 || !n.endsWith(".obb")) continue;
                    File o = ownObbFile(kind), tmp = new File(o.getPath() + ".part");
                    InputStream in = getContentResolver().openInputStream(uris.get(i)); OutputStream out = new FileOutputStream(tmp);
                    byte[] b = new byte[1 << 20]; int r; long last = 0;
                    while ((r = in.read(b)) > 0) { out.write(b, 0, r); done += r; if (done - last > (8 << 20)) { last = done; progress(id, "copy", done, total, kind); } }
                    in.close(); out.close(); o.delete(); tmp.renameTo(o); ok++;
                }
                progress(id, ok > 0 ? "done" : "error", total, total, ok > 0 ? ok + " game file(s) imported" : "Pick the main.8 / patch.8 game files");
            } catch (Exception e) { progress(id, "error", done, total, String.valueOf(e.getMessage())); }
        }}).start();
    }
    void askObbAccess() {
        runOnUiThread(new Runnable() { public void run() {
            try {
                if (Build.VERSION.SDK_INT >= 23) requestPermissions(new String[] { "android.permission.READ_EXTERNAL_STORAGE", "android.permission.WRITE_EXTERNAL_STORAGE" }, 7);
                // Android 11+: an app allowed to install apps may read other apps' Android/obb
                if (Build.VERSION.SDK_INT >= 26 && !getPackageManager().canRequestPackageInstalls())
                    startActivity(new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:" + getPackageName())));
            } catch (Exception e) { toast("Open Settings > Apps > AML Multiplayer > Install unknown apps"); }
        }});
    }

    // ---- 1.1: the MP files (files/TYFMC/MP) for the GTA SA inside Christe, from the wagu repo -----------
    void downloadMpFiles(String id) {
        File to = new File(gameData(), "files/TYFMC/MP"); to.mkdirs();
        long done = 0, total = 0; int n = 0;
        try {
            HttpURLConnection c = (HttpURLConnection) new URL("https://raw.githubusercontent.com/JebetrexxMC/wagu/main/assets/MP/MP.zip").openConnection();
            c.setConnectTimeout(10000); c.setReadTimeout(30000); c.setRequestProperty("User-Agent", "AMLMultiplayer/1.3 (Android)");
            total = c.getContentLength();
            final long[] got = { 0 };
            final long tot = total; final String fid = id;
            InputStream raw = new java.io.FilterInputStream(c.getInputStream()) {
                long last = 0;
                @Override public int read(byte[] b, int o, int l) throws java.io.IOException { int r = super.read(b, o, l); if (r > 0) { got[0] += r; if (got[0] - last > (2 << 20)) { last = got[0]; progress(fid, "download", got[0], tot, "MP files"); } } return r; }
            };
            ZipInputStream z = new ZipInputStream(new java.io.BufferedInputStream(raw, 1 << 16));
            String base = to.getCanonicalPath();
            for (ZipEntry e; (e = z.getNextEntry()) != null;) {
                String nm = e.getName(); int at = nm.indexOf("assets/MP/"), cut = 10;
                if (at < 0) { at = nm.indexOf("TYFMC/MP/"); cut = 9; }
                if (at < 0) continue;
                String rel = nm.substring(at + cut); if (rel.length() == 0) continue;
                File f = new File(to, rel); if (!f.getCanonicalPath().startsWith(base)) continue;
                if (e.isDirectory()) { f.mkdirs(); continue; }
                f.getParentFile().mkdirs();
                OutputStream out = new FileOutputStream(f); byte[] b = new byte[65536]; int r;
                while ((r = z.read(b)) > 0) out.write(b, 0, r);
                out.close(); n++;
            }
            z.close(); c.disconnect();
            progress(id, "done", total, total, n + " MP files ready");
        } catch (Exception e) { progress(id, "error", done, total, String.valueOf(e.getMessage())); }
    }

    // ---- 1.1: LAN servers (MP's own query, the same as the game: PKT_QUERY 14 -> PKT_INFO 15) ---------
    void lanScan(String id) {
        JSONArray out = new JSONArray();
        try {
            DatagramSocket s = new DatagramSocket(); s.setBroadcast(true); s.setSoTimeout(150);
            byte[] q = new byte[5]; int t0 = (int) (System.currentTimeMillis() & 0x7fffffff);
            q[0] = 14; q[1] = (byte) t0; q[2] = (byte) (t0 >> 8); q[3] = (byte) (t0 >> 16); q[4] = (byte) (t0 >> 24);
            int[] ports = { 1922, 1923, 1924, 1930, 1931, 1932, 1933, 1934 };
            java.util.LinkedHashSet<String> hosts = new java.util.LinkedHashSet<String>();
            hosts.add("255.255.255.255"); hosts.add("192.168.43.1"); hosts.add("192.168.49.1"); hosts.add("172.20.10.1"); hosts.add("127.0.0.1");
            try {
                for (Enumeration<java.net.NetworkInterface> en = java.net.NetworkInterface.getNetworkInterfaces(); en.hasMoreElements();) {
                    java.net.NetworkInterface ni = en.nextElement();
                    if (!ni.isUp() || ni.isLoopback() || ni.getName().startsWith("rmnet") || ni.getName().startsWith("ccmni")) continue;
                    for (java.net.InterfaceAddress ia : ni.getInterfaceAddresses()) {
                        if (!(ia.getAddress() instanceof java.net.Inet4Address)) continue;
                        if (ia.getBroadcast() != null) hosts.add(ia.getBroadcast().getHostAddress());
                        byte[] a = ia.getAddress().getAddress();
                        for (int h = 1; h < 255; h++) hosts.add((a[0] & 255) + "." + (a[1] & 255) + "." + (a[2] & 255) + "." + h);
                    }
                }
            } catch (Exception e) {}
            for (int port : ports) for (String h : hosts) { try { s.send(new DatagramPacket(q, q.length, InetAddress.getByName(h), port)); } catch (Exception e) {} }
            java.util.HashSet<String> seen = new java.util.HashSet<String>();
            long end = System.currentTimeMillis() + 1800;
            byte[] b = new byte[1024];
            while (System.currentTimeMillis() < end) {
                DatagramPacket p = new DatagramPacket(b, b.length);
                try { s.receive(p); } catch (Exception e) { continue; }
                int len = p.getLength(); if (len < 42 || b[0] != 15) continue;
                String key = p.getAddress().getHostAddress() + ":" + p.getPort(); if (!seen.add(key)) continue;
                JSONObject o = new JSONObject();
                o.put("host", p.getAddress().getHostAddress()); o.put("port", p.getPort());
                int rt = (b[1] & 255) | ((b[2] & 255) << 8) | ((b[3] & 255) << 16) | ((b[4] & 255) << 24);
                o.put("ping", Math.max(1, (int) ((System.currentTimeMillis() & 0x7fffffff) - rt)));
                o.put("players", b[7] & 255); o.put("max", b[8] & 255); o.put("region", b[9] & 255);
                o.put("name", cstr(b, 10, 32));
                if (len >= 43) o.put("locked", b[42] != 0);
                if (len >= 44) o.put("icon", b[43] & 255);
                if (len >= 68) o.put("lang", cstr(b, 44, 24));
                if (len >= 72 + 8 + 1 + 24) { o.put("cc", cstr(b, 72, 8)); o.put("modeCol", b[80] & 255); o.put("mode", cstr(b, 81, 24)); }
                if (len >= 105 + 96) o.put("tags", cstr(b, 105, 96));
                if (len >= 201 + 160) o.put("desc", cstr(b, 201, 160));
                out.put(o);
            }
            s.close();
        } catch (Exception e) {}
        js("window.CX_lan && CX_lan(" + q(id) + "," + q(out.toString()) + ")");
    }
    static String cstr(byte[] b, int at, int n) {
        int e = at; while (e < at + n && e < b.length && b[e] != 0) e++;
        try { return new String(b, at, e - at, "UTF-8"); } catch (Exception x) { return ""; }
    }
}
