package com.christe.mp;

import android.app.Application;
import android.content.Context;
import android.os.Build;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.lang.reflect.Field;
import java.lang.reflect.Method;

// Christe 1.1: the app object of both processes. In ":game" (the GTA SA inside Christe):
//  - Android 5-8: the game's classes are put under Christe's class loader here (Android 9+: ChristeFactory)
//  - a crash of the game is written to date2/logs/game_crash.txt (the launcher shows it next time)
public class ChristeApp extends Application {
    static boolean game;

    @Override protected void attachBaseContext(Context base) {
        super.attachBaseContext(base);
        game = GameEnv.isGameProcess();
        if (!game) return;
        if (Build.VERSION.SDK_INT < 28) {
            try {
                ClassLoader mine = ChristeApp.class.getClassLoader();
                ClassLoader g = GameEnv.loader(base.getApplicationInfo().dataDir, mine.getParent());
                if (g != null) {
                    Field f = ClassLoader.class.getDeclaredField("parent");
                    f.setAccessible(true);
                    f.set(mine, g);
                }
            } catch (Throwable t) { crash("class loader", t); }
        }
        final Thread.UncaughtExceptionHandler def = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler(new Thread.UncaughtExceptionHandler() {
            public void uncaughtException(Thread th, Throwable e) {
                crash("thread " + th.getName(), e);
                if (def != null) def.uncaughtException(th, e);
            }
        });
    }

    // 1.3: what the AML GTA SA loaders do in their own Application (com.fastman92.main_activity_launcher.Application):
    // read the fastman92 launcher settings from the manifest (LAUNCHER_* meta-data). Without this the game builds
    // its OBB name from an empty package ("main.8.null.obb") and never finds the game files.
    // Then the game files folder the launcher picked (christe_game.txt line 5) replaces the OBB folder.
    @Override public void onCreate() {
        super.onCreate();
        if (!game) return;
        try {
            ClassLoader cl = getClassLoader();
            Class<?> sl = Class.forName("com.fastman92.main_activity_launcher.SettingsLoader", true, cl);
            Method m = null;
            for (Method x : sl.getDeclaredMethods()) if (x.getName().equals("InitializeSettings")) m = x;
            if (m != null) {
                m.setAccessible(true);
                Class<?>[] pt = m.getParameterTypes();
                Object[] args = new Object[pt.length];
                for (int i = 0; i < pt.length; i++) args[i] = pt[i] == boolean.class ? Boolean.FALSE : this;
                m.invoke(null, args);
            }
            if (GameEnv.obbDir == null || GameEnv.obbDir.length() == 0) GameEnv.load(getApplicationInfo().dataDir);
            if (GameEnv.obbDir != null && GameEnv.obbDir.length() > 0) {
                Class<?> st = Class.forName("com.fastman92.main_activity_launcher.Settings", true, cl);
                Field f = st.getDeclaredField("AndroidOBBdirectory");
                f.setAccessible(true);
                f.set(null, GameEnv.obbDir);
            }
        } catch (Throwable t) { crash("fastman92 settings (not an AML GTA SA?)", t); }
    }

    void crash(String where, Throwable t) {
        try {
            File ext = getExternalFilesDir(null);
            File d = new File(ext != null ? ext.getParentFile() : getFilesDir(), "date2/logs");
            d.mkdirs();
            StringWriter sw = new StringWriter(); t.printStackTrace(new PrintWriter(sw));
            FileWriter w = new FileWriter(new File(d, "game_crash.txt"));
            w.write(System.currentTimeMillis() + "\n" + where + "\n" + GameEnv.pkg + " " + GameEnv.abi + "\n" + sw);
            w.close();
        } catch (Throwable x) {}
    }
}
