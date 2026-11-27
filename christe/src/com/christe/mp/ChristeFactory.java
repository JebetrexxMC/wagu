package com.christe.mp;

import android.app.AppComponentFactory;
import android.app.Application;
import android.content.pm.ApplicationInfo;

import dalvik.system.PathClassLoader;

// Christe 1.1 (Android 9+): in the ":game" process Christe's classes sit on top of the installed GTA SA's,
// so com.christe.mp.Game (extends com.rockstargames.gtasa.GTASA) finds its parent class and the game's
// own System.loadLibrary("GTASA") finds the game's native libraries (the DexClassLoader's library path).
public class ChristeFactory extends AppComponentFactory {
    @Override public ClassLoader instantiateClassLoader(ClassLoader cl, ApplicationInfo ai) {
        try {
            String p = Application.getProcessName();
            if (p == null || !p.endsWith(":game")) return cl;
            ClassLoader game = GameEnv.loader(ai.dataDir, cl.getParent());
            if (game == null) return cl;
            StringBuilder dex = new StringBuilder(ai.sourceDir);
            if (ai.splitSourceDirs != null) for (String s : ai.splitSourceDirs) dex.append(':').append(s);
            return new PathClassLoader(dex.toString(), ai.nativeLibraryDir, game);
        } catch (Throwable t) { return cl; }
    }
}
