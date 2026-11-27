package com.christe.mp;

import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.content.res.Resources;
import android.os.Bundle;
import android.os.Environment;
import android.view.WindowManager;

import java.io.File;

// Christe 1.1: GTA San Andreas inside Christe Multiplayer (process ":game", its own task), not the GTA SA app.
// The game's code is the installed AML GTA SA's (com.rockstargames.gtasa.GTASA, loaded from its APK at run time);
// Christe gives it:
//  - the game's own resources / assets / APK path (its R ids and its assets/ are the game's)
//  - the OBB: Christe's own Android/obb/com.christe.mp (main.8 / patch.8, copied or imported from the game's),
//    else the game's own folder when it can be read
//  - AML: Android/data/com.christe.mp/mods (libMP copied there by the launcher), files, configs
//  - the server you picked: Intent extras christe_join / christe_nick, read by libMP 5.02.76+ on the title screen
public class Game extends com.rockstargames.gtasa.GTASA {
    Resources res; AssetManager assets; ApplicationInfo gameAi;

    ApplicationInfo gai() {
        if (gameAi == null) {
            try {
                if (GameEnv.pkg == null || GameEnv.pkg.length() == 0) GameEnv.load(super.getApplicationInfo().dataDir);
                gameAi = super.getPackageManager().getApplicationInfo(GameEnv.pkg, 0);
            } catch (Throwable t) {}
        }
        return gameAi;
    }
    @Override public Resources getResources() {
        if (res == null) {
            try { if (gai() != null) res = super.getPackageManager().getResourcesForApplication(gai()); } catch (Throwable t) {}
            if (res == null) return super.getResources();
        }
        return res;
    }
    @Override public AssetManager getAssets() { Resources r = getResources(); return r != null ? r.getAssets() : super.getAssets(); }
    @Override public String getPackageCodePath() { return gai() != null ? gai().sourceDir : super.getPackageCodePath(); }
    @Override public String getPackageResourcePath() { return gai() != null ? gai().publicSourceDir : super.getPackageResourcePath(); }

    @Override protected void onCreate(Bundle b) {
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON | WindowManager.LayoutParams.FLAG_FULLSCREEN);
        super.onCreate(b);
    }

    // the OBB the game opens (native code asks Java for it): 1 = main, 2 = patch
    @Override public String FileGetArchiveName(int type) {
        String orig = null;
        try { orig = super.FileGetArchiveName(type); } catch (Throwable t) {}
        boolean patch = orig != null ? orig.contains("patch") : type == 2;
        String kind = patch ? "patch" : "main";
        String sd = Environment.getExternalStorageDirectory().getAbsolutePath();
        File own = getObbDir();
        String ownDir = own != null ? own.getAbsolutePath() : sd + "/Android/obb/com.christe.mp";
        String g = GameEnv.pkg != null && GameEnv.pkg.length() > 0 ? GameEnv.pkg : "com.rockstargames.gtasa";
        String[] c = {
            ownDir + "/" + kind + ".8.com.christe.mp.obb",
            ownDir + "/" + kind + ".8." + g + ".obb",
            sd + "/Android/obb/" + g + "/" + kind + ".8." + g + ".obb",
            sd + "/Android_unprotected/obb/" + g + "/" + kind + ".8." + g + ".obb",
            sd + "/Android_unprotected/data/" + g + "/obb/" + kind + ".8." + g + ".obb",
        };
        if (orig != null && new File(orig).canRead()) return orig;
        for (String p : c) { File f = new File(p); if (f.isFile() && f.canRead()) return p; }
        return orig != null ? orig : c[0];
    }
}
