package com.thirtyfoxmc.viceloader;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

/** VICE Loader start screen: overlay permission, start the overlay, launch GTA Vice City, export the mods. No login. */
public class MainActivity extends Activity {
    static final String[] VC_PACKAGES = { "com.rockstargames.gtavc", "com.rockstargames.gtavc.de", "com.rockstargames.gtavcde" };
    private TextView mStatus;

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        CrashLog.install(this);
        CrashLog.exportPending(this);   // 4.13.64: a crash last time -> Download/ViceLoader/crash_<time>.txt
        if (Build.VERSION.SDK_INT >= 33) requestPermissions(new String[] { "android.permission.POST_NOTIFICATIONS" }, 1);
        if (Build.VERSION.SDK_INT <= 28) requestPermissions(new String[] { "android.permission.WRITE_EXTERNAL_STORAGE" }, 2);
        float d = getResources().getDisplayMetrics().density;
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding((int) (24 * d), (int) (24 * d), (int) (24 * d), (int) (24 * d));
        col.setBackgroundColor(Color.rgb(26, 29, 34));
        TextView t = new TextView(this);
        t.setText("VICE Loader");
        t.setTextColor(Color.WHITE); t.setTextSize(28); t.setTypeface(Typeface.DEFAULT_BOLD);
        col.addView(t);
        TextView s = new TextView(this);
        s.setText("MP Launcher v4.13.64 for GTA Vice City - the San Andreas MP floating bar (menu, chat, voice, MonetLoader), Internet server list with the 24/7 VICE City LAN server, chat, virtual keyboard, settings and about pages as a display-over-apps overlay. AML, SAUtils and MP with MonetLoader built in. No login.");
        s.setTextColor(Color.rgb(150, 157, 166)); s.setTextSize(14); s.setPadding(0, (int) (8 * d), 0, (int) (16 * d));
        col.addView(s);
        mStatus = new TextView(this);
        mStatus.setTextColor(Color.rgb(126, 178, 226)); mStatus.setTextSize(14); mStatus.setPadding(0, 0, 0, (int) (12 * d));
        col.addView(mStatus);
        col.addView(button("1.  Allow display over other apps", v -> {
            if (Build.VERSION.SDK_INT >= 23) startActivity(new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION, Uri.parse("package:" + getPackageName())));
        }, false));
        col.addView(button("2.  Start VICE Loader overlay", v -> startOverlay(), true));
        col.addView(button("3.  Launch GTA Vice City", v -> { startOverlay(); launchGame(this); }, true));
        col.addView(button("Open the launcher now", v -> {
            startOverlay();
            startService(new Intent(this, OverlayService.class).setAction(OverlayService.ACT_PANEL));
        }, false));
        col.addView(button("Export AML / SAUtils / MP / MonetLoader to Downloads", v -> com.thirtyfoxmc.viceloader.Bundle.exportAsync(this), false));
        col.addView(button("Save log to Downloads (for a bug report)", v -> CrashLog.saveNow(this), false));
        col.addView(button("Stop the overlay", v -> startService(new Intent(this, OverlayService.class).setAction(OverlayService.ACT_STOP)), false));
        TextView h = new TextView(this);
        h.setText("\nFloating bar: \u22ee = menu, chat = chat keyboard, mic = voice chat (hold = voice page), moon = MonetLoader, drag = move.\nThe game needs MP 4.13.64+ (libMP in the AML mods folder) for the bridge on 127.0.0.1:47790.\n\nInside the app:\n" + com.thirtyfoxmc.viceloader.Bundle.describe(this));
        h.setTextColor(Color.rgb(150, 157, 166)); h.setTextSize(12);
        col.addView(h);
        ScrollView sv = new ScrollView(this);
        sv.addView(col);
        setContentView(sv);
        handleOpen(getIntent());
    }

    @Override protected void onResume() {
        super.onResume();
        boolean ok = OverlayService.canDraw(this);
        mStatus.setText(ok ? (OverlayService.sInstance != null ? "Overlay: running" : "Overlay permission: allowed") : "Overlay permission: not allowed yet (step 1)");
    }

    @Override protected void onNewIntent(Intent i) { super.onNewIntent(i); setIntent(i); handleOpen(i); }

    /**
     * 4.13.64: adb / emulator test hook, e.g.
     *   am start -n com.thirtyfoxmc.viceloader/.MainActivity --es open panel     (bar | panel | chat | menu | page:N | stop | savelog)
     *   am start -n com.thirtyfoxmc.viceloader/.MainActivity --es crashtest native   (java | native: checks the crash log)
     */
    private void handleOpen(Intent in) {
        if (in == null) return;
        String o = in.getStringExtra("open"), ct = in.getStringExtra("crashtest");
        if (o == null && ct == null) return;
        if ("savelog".equals(o)) { CrashLog.saveNow(this); return; }
        if ("stop".equals(o)) { startService(new Intent(this, OverlayService.class).setAction(OverlayService.ACT_STOP)); return; }
        startOverlay();
        Intent s = new Intent(this, OverlayService.class).setAction(OverlayService.ACT_OPEN);
        int m = -1;
        if ("bar".equals(o)) m = Native.MODE_BUBBLE;
        else if ("panel".equals(o)) m = Native.MODE_PANEL;
        else if ("chat".equals(o)) m = Native.MODE_CHAT;
        else if ("menu".equals(o)) m = Native.MODE_MENU;
        else if (o != null && o.startsWith("page:")) { try { m = 100 + Integer.parseInt(o.substring(5).trim()); } catch (Exception ignored) {} }
        if (m >= 0) s.putExtra("mode", m);
        if (ct != null) s.putExtra("crashtest", ct);
        startService(s);
    }

    private Button button(String text, View.OnClickListener l, boolean primary) {
        float d = getResources().getDisplayMetrics().density;
        Button b = new Button(this);
        b.setText(text); b.setAllCaps(false); b.setTextSize(15);
        b.setTextColor(primary ? Color.WHITE : Color.rgb(226, 229, 233));
        GradientDrawable g = new GradientDrawable();
        g.setColor(primary ? Color.rgb(76, 160, 48) : Color.argb(40, 236, 232, 225));
        g.setStroke((int) (1.5f * d), primary ? Color.argb(160, 150, 220, 120) : Color.argb(70, 236, 232, 225));
        b.setBackground(g);
        b.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, (int) (54 * d));
        lp.bottomMargin = (int) (10 * d);
        b.setLayoutParams(lp);
        b.setOnClickListener(l);
        return b;
    }

    private void startOverlay() {
        if (!OverlayService.canDraw(this)) { Toast.makeText(this, "Allow \"Display over other apps\" first", Toast.LENGTH_LONG).show(); return; }
        Intent i = new Intent(this, OverlayService.class);
        if (Build.VERSION.SDK_INT >= 26) startForegroundService(i); else startService(i);
        mStatus.setText("Overlay: running");
    }

    static void launchGame(Context c) {
        for (String p : VC_PACKAGES) {
            Intent i = c.getPackageManager().getLaunchIntentForPackage(p);
            if (i != null) { i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK); c.startActivity(i); return; }
        }
        Toast.makeText(c, "GTA Vice City is not installed", Toast.LENGTH_LONG).show();
    }
}
