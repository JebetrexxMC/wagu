package com.thirtyfoxmc.viceloader;

import android.content.res.AssetManager;

/** libviceloader.so: Dear ImGui (the San Andreas MP launcher look), the MP keyboard, the server list + online lobby (24/7 VICE City LAN server). */
public final class Native {
    static { System.loadLibrary("viceloader"); }
    public static final int MODE_BUBBLE = 0, MODE_PANEL = 1, MODE_CHAT = 2, MODE_MENU = 3, MODE_PAGE = 4;   // 3 = the ⋮ dropdown, 4 = a San Andreas page window (Player list); open(100 + tab) = a launcher page

    static native void init(AssetManager assets, String filesDir);
    static native void surfaceCreated();
    static native void surfaceChanged(int w, int h);
    /** action: 0 down, 1 up, 2 move */
    static native void touch(int action, float x, float y);
    static native int render();
    static native int mode();
    static native void open(int mode);
    static native void paste(String text);
    static native void setPref(String key, String value);

    /** called by the native side (GL thread) */
    static void onNative(String what, String arg) {
        OverlayService s = OverlayService.sInstance;
        if (s != null) s.onNative(what, arg);
    }
}
