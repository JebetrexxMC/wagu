package com.christe.mp;

import android.content.Context;
import android.content.pm.ApplicationInfo;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.FileWriter;

import dalvik.system.DexClassLoader;

// Christe 1.1: the installed GTA SA that runs inside Christe (the ":game" process).
// MainActivity writes files/christe_game.txt before it starts the game:
//   line 1 the game's package, line 2 its APK (+ ":" splits), line 3 the native library path, line 4 the ABI
// The ":game" process reads it to load the game's classes (DexClassLoader) under Christe's own.
final class GameEnv {
    static String pkg = "", apk = "", libs = "", abi = "";
    static ClassLoader game;

    static File file(String dataDir) { return new File(dataDir, "files/christe_game.txt"); }

    static void save(Context c, String pkg, ApplicationInfo ai, String abi, String own) throws Exception {
        StringBuilder a = new StringBuilder(ai.sourceDir);
        if (ai.splitSourceDirs != null) for (String s : ai.splitSourceDirs) a.append(':').append(s);
        String abiDir = abi.startsWith("arm64") ? "arm64-v8a" : "armeabi-v7a";
        StringBuilder l = new StringBuilder(own);   // 1.2: the unpacked libs first
        if (ai.nativeLibraryDir != null) l.append(':').append(ai.nativeLibraryDir);
        l.append(':').append(ai.sourceDir).append("!/lib/").append(abiDir);
        if (ai.splitSourceDirs != null) for (String s : ai.splitSourceDirs) l.append(':').append(s).append("!/lib/").append(abiDir);
        File f = file(c.getApplicationInfo().dataDir);
        f.getParentFile().mkdirs();
        FileWriter w = new FileWriter(f);
        w.write(pkg + "\n" + a + "\n" + l + "\n" + abi + "\n");
        w.close();
    }

    static boolean load(String dataDir) {
        try {
            BufferedReader r = new BufferedReader(new FileReader(file(dataDir)));
            pkg = r.readLine(); apk = r.readLine(); libs = r.readLine(); abi = r.readLine();
            r.close();
            return pkg != null && apk != null && new File(apk.split(":")[0]).isFile();
        } catch (Exception e) { return false; }
    }

    static ClassLoader loader(String dataDir, ClassLoader parent) {
        if (game != null) return game;
        if (!load(dataDir)) return null;
        File opt = new File(dataDir, "code_cache/game");
        opt.mkdirs();
        game = new DexClassLoader(apk, opt.getAbsolutePath(), libs, parent);
        return game;
    }

    static boolean isGameProcess() {
        try {
            BufferedReader r = new BufferedReader(new FileReader("/proc/self/cmdline"));
            String n = r.readLine(); r.close();
            return n != null && n.trim().replace("\0", "").endsWith(":game");
        } catch (Exception e) { return false; }
    }
}
