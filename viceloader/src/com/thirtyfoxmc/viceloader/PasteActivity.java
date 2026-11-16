package com.thirtyfoxmc.viceloader;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;

/** Reads the clipboard for the MP keyboard's PASTE key (needs window focus on Android 10+), then closes. */
public class PasteActivity extends Activity {
    private boolean mDone;
    @Override public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (!hasFocus || mDone) return;
        mDone = true;
        String t = "";
        try {
            ClipboardManager cm = (ClipboardManager) getSystemService(CLIPBOARD_SERVICE);
            ClipData d = cm != null ? cm.getPrimaryClip() : null;
            if (d != null && d.getItemCount() > 0) { CharSequence s = d.getItemAt(0).coerceToText(this); if (s != null) t = s.toString(); }
        } catch (Exception ignored) {}
        OverlayService s = OverlayService.sInstance;
        if (s != null) s.deliverPaste(t);
        finish();
        overridePendingTransition(0, 0);
    }
}
