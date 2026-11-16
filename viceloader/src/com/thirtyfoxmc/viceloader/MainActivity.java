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
        s.setText("MP Launcher v4.13.63 for GTA Vice City - the San Andreas MP floating menu, server list, chat, virtual keyboard and the VICE City LAN server as a display-over-apps overlay. AML, SAUtils, MP and MonetLoader for MP inside. No login.");
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
        col.addView(button("Stop the overlay", v -> startService(new Intent(this, OverlayService.class).setAction(OverlayService.ACT_STOP)), false));
        TextView h = new TextView(this);
        h.setText("\nBubble: tap = MP launcher, hold = chat keyboard, drag = move.\nThe game needs MP 4.13.63+ (libMP in the AML mods folder) for the bridge on 127.0.0.1:47790.\n\nInside the app:\n" + com.thirtyfoxmc.viceloader.Bundle.describe(this));
        h.setTextColor(Color.rgb(150, 157, 166)); h.setTextSize(12);
        col.addView(h);
        ScrollView sv = new ScrollView(this);
        sv.addView(col);
        setContentView(sv);
    }

    @Override protected void onResume() {
        super.onResume();
        boolean ok = OverlayService.canDraw(this);
        mStatus.setText(ok ? (OverlayService.sInstance != null ? "Overlay: running" : "Overlay permission: allowed") : "Overlay permission: not allowed yet (step 1)");
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
