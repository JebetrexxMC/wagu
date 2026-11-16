package com.thirtyfoxmc.viceloader;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Path;
import android.graphics.PorterDuff;
import android.graphics.PorterDuffColorFilter;
import android.media.AudioAttributes;
import android.media.SoundPool;
import android.net.Uri;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.graphics.RectF;
import android.opengl.GLSurfaceView;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.provider.Settings;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;
import android.widget.Toast;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/**
 * The "display over other apps" part of VICE Loader: the San Andreas MP floating bar ([⋮] [chat] [mic] [MonetLoader])
 * and, while the launcher, the ⋮ dropdown or the chat keyboard is open, a full screen translucent GLES3 window with the
 * ImGui launcher over GTA Vice City. While only the bubble shows, the big window is invisible and lets every touch
 * through to the game.
 */
public class OverlayService extends Service {
    static volatile OverlayService sInstance;
    static final String CH = "viceloader";
    static final String ACT_STOP = "com.thirtyfoxmc.viceloader.STOP", ACT_PANEL = "com.thirtyfoxmc.viceloader.PANEL", ACT_CHAT = "com.thirtyfoxmc.viceloader.CHAT", ACT_OPEN = "com.thirtyfoxmc.viceloader.OPEN";

    private final Handler mMain = new Handler(Looper.getMainLooper());
    private WindowManager mWm;
    private GLSurfaceView mGl;
    private WindowManager.LayoutParams mGlLp, mBubbleLp;
    private BarView mBubble;
    private volatile int mMode = -1;
    private boolean mGlTouchable = true;

    public static boolean canDraw(Context c) { return Build.VERSION.SDK_INT < 23 || Settings.canDrawOverlays(c); }
    @Override public IBinder onBind(Intent i) { return null; }

    @Override public void onCreate() {
        super.onCreate();
        sInstance = this;
        CrashLog.install(this);   // 4.13.64: overlay crash log
        Native.init(getAssets(), getFilesDir().getAbsolutePath());
        Native.setPref("bundle", Bundle.describe(this));
        startForegroundNote();
        if (!canDraw(this)) { Toast.makeText(this, "VICE Loader needs \"Display over other apps\"", Toast.LENGTH_LONG).show(); stopSelf(); return; }
        mWm = (WindowManager) getSystemService(WINDOW_SERVICE);
        addGlWindow();
        addBubble();
        setMode(Native.MODE_BUBBLE);
        mMain.post(mTick);
    }

    @Override public int onStartCommand(Intent intent, int flags, int startId) {
        String a = intent != null ? intent.getAction() : null;
        if (ACT_STOP.equals(a)) { stopSelf(); return START_NOT_STICKY; }
        if (ACT_PANEL.equals(a)) openFromBubble(Native.MODE_PANEL);
        if (ACT_CHAT.equals(a)) openFromBubble(Native.MODE_CHAT);
        if (ACT_OPEN.equals(a)) {   // 4.13.64: MainActivity "open" extra (adb / emulator test): any mode or 100 + page
            int m = intent.getIntExtra("mode", Native.MODE_PANEL);
            if (m == Native.MODE_BUBBLE) { if (mGl != null) mGl.queueEvent(() -> Native.open(Native.MODE_BUBBLE)); setMode(Native.MODE_BUBBLE); }
            else openFromBubble(m);
        }
        if (intent != null && intent.hasExtra("crashtest") && mGl != null) {   // checks the crash log (vl_crash.cpp / CrashLog.java)
            String k = intent.getStringExtra("crashtest");
            if ("native".equals(k)) mGl.queueEvent(() -> Native.setPref("crashtest", "1"));
            else mMain.post(() -> { throw new RuntimeException("VICE Loader crash test"); });
        }
        return START_STICKY;
    }

    @Override public void onDestroy() {
        mMain.removeCallbacks(mTick);
        if (mSounds != null) mSounds.release();
        try { if (mBubble != null) mWm.removeView(mBubble); } catch (Exception ignored) {}
        try { if (mGl != null) mWm.removeView(mGl); } catch (Exception ignored) {}
        sInstance = null;
        super.onDestroy();
    }

    private static int overlayType() {
        return Build.VERSION.SDK_INT >= 26 ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY : WindowManager.LayoutParams.TYPE_PHONE;
    }

    private void startForegroundNote() {
        NotificationManager nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        if (Build.VERSION.SDK_INT >= 26) nm.createNotificationChannel(new NotificationChannel(CH, "VICE Loader overlay", NotificationManager.IMPORTANCE_LOW));
        int pf = Build.VERSION.SDK_INT >= 23 ? PendingIntent.FLAG_IMMUTABLE : 0;
        PendingIntent open = PendingIntent.getService(this, 1, new Intent(this, OverlayService.class).setAction(ACT_PANEL), pf);
        PendingIntent chat = PendingIntent.getService(this, 2, new Intent(this, OverlayService.class).setAction(ACT_CHAT), pf);
        PendingIntent stop = PendingIntent.getService(this, 3, new Intent(this, OverlayService.class).setAction(ACT_STOP), pf);
        Notification.Builder b = Build.VERSION.SDK_INT >= 26 ? new Notification.Builder(this, CH) : new Notification.Builder(this);
        b.setSmallIcon(R.drawable.icon).setContentTitle("VICE Loader").setContentText("MP launcher over GTA Vice City - use the floating bar")
         .setContentIntent(open).setOngoing(true)
         .addAction(0, "Launcher", open).addAction(0, "Chat", chat).addAction(0, "Stop", stop);
        Notification n = b.build();
        if (Build.VERSION.SDK_INT >= 34) startForeground(1, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE);
        else startForeground(1, n);
    }

    /** The ImGui surface: touches go to the native side (ImGui input queue). */
    static final class TouchGl extends GLSurfaceView {
        TouchGl(Context c) { super(c); }
        @Override public boolean onTouchEvent(MotionEvent e) {
            int a;
            switch (e.getActionMasked()) {
                case MotionEvent.ACTION_DOWN: a = 0; break;
                case MotionEvent.ACTION_UP: case MotionEvent.ACTION_CANCEL: a = 1; break;
                case MotionEvent.ACTION_MOVE: a = 2; break;
                default: return true;
            }
            Native.touch(a, e.getX(0), e.getY(0));
            requestRender();
            return true;
        }
    }

    // ---------------------------------------------------------------- the ImGui window
    private void addGlWindow() {
        mGl = new TouchGl(this);
        mGl.setEGLContextClientVersion(3);
        mGl.setEGLConfigChooser(8, 8, 8, 8, 0, 0);
        mGl.getHolder().setFormat(PixelFormat.TRANSLUCENT);
        mGl.setZOrderOnTop(true);
        mGl.setPreserveEGLContextOnPause(true);
        mGl.setRenderer(new GLSurfaceView.Renderer() {
            @Override public void onSurfaceCreated(GL10 gl, EGLConfig c) { Native.surfaceCreated(); }
            @Override public void onSurfaceChanged(GL10 gl, int w, int h) { Native.surfaceChanged(w, h); }
            @Override public void onDrawFrame(GL10 gl) {
                final int m = Native.render();
                if (m != mMode) { mPendingMode = m; mMain.post(mApplyMode); }
            }
        });
        mGl.setRenderMode(GLSurfaceView.RENDERMODE_WHEN_DIRTY);
        mGlLp = new WindowManager.LayoutParams(WindowManager.LayoutParams.MATCH_PARENT, WindowManager.LayoutParams.MATCH_PARENT, overlayType(),
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN
                        | WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS | WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED,
                PixelFormat.TRANSLUCENT);
        mGlLp.gravity = Gravity.TOP | Gravity.LEFT;
        if (Build.VERSION.SDK_INT >= 28) mGlLp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        mWm.addView(mGl, mGlLp);
    }

    /** bubble: the GL window is see-through and untouchable (alpha 0 so Android 12+ passes the touches to the game) */
    private void setMode(int m) {
        if (m == mMode || mGl == null) return;
        mMode = m;
        boolean big = m != Native.MODE_BUBBLE;
        if (big != mGlTouchable || mGlLp.alpha != (big ? 1f : 0f)) {
            mGlTouchable = big;
            if (big) mGlLp.flags &= ~WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE;
            else mGlLp.flags |= WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE;
            mGlLp.alpha = big ? 1f : 0f;
            try { mWm.updateViewLayout(mGl, mGlLp); } catch (Exception ignored) {}
        }
        if (mBubble != null) mBubble.setVisibility(mBarShown && (m == Native.MODE_BUBBLE || m == Native.MODE_MENU) ? View.VISIBLE : View.GONE);   // the bar stays over the ⋮ dropdown, like San Andreas
    }

    private volatile int mPendingMode;
    private final Runnable mApplyMode = new Runnable() { @Override public void run() { setMode(mPendingMode); } };

    private final Runnable mTick = new Runnable() {
        @Override public void run() {
            if (mGl != null) mGl.requestRender();
            mMain.postDelayed(this, mMode == Native.MODE_BUBBLE ? 250 : 16);   // the bridge keeps running while collapsed
        }
    };

    void openFromBubble(final int mode) {
        if (mGl == null) return;
        mGl.queueEvent(() -> Native.open(mode));
        setMode(mode >= 100 ? Native.MODE_PANEL : mode);
        mGl.requestRender();
    }

    // ---------------------------------------------------------------- the floating bar (San Andreas MP TopbarPlus)
    // 4.13.64: the same bar as San Andreas MP (src/ui/floatmenu.cpp): four square see-through pills
    //   [⋮] menu (Help / Server / Player list dropdown)  [chat] chat keyboard  [mic] voice chat  [moon] MonetLoader
    // drag any pill = move the whole bar (saved); a long press on the mic opens the Voice chat page.
    static final int PILL_DOTS = 0, PILL_CHAT = 1, PILL_MIC = 2, PILL_MONET = 3, PILLS = 4;
    static final int TAB_PLAYERS = 6, TAB_VOICE = 7, TAB_MONET = 8;   // vl_panel.cpp VT_*
    private float mBarSize = 1f;
    private volatile int mMic = 0;            // MIC <0 off-server, 1 off, 2 on, 3 talking> from the game
    private SoundPool mSounds; private int mClickId = 0; private float mClickVol = 0.8f;

    private int pillPx() { return (int) (44 * getResources().getDisplayMetrics().density * mBarSize); }
    private int gapPx() { return (int) (8 * getResources().getDisplayMetrics().density * mBarSize); }

    private void addBubble() {
        mBubble = new BarView(this);
        mBubbleLp = new WindowManager.LayoutParams(barW(), pillPx(), overlayType(),
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN, PixelFormat.TRANSLUCENT);
        mBubbleLp.gravity = Gravity.TOP | Gravity.LEFT;
        int sw = getResources().getDisplayMetrics().widthPixels;
        mBubbleLp.x = getSharedPreferences("vl", 0).getInt("bx", (sw - barW()) / 2);
        mBubbleLp.y = getSharedPreferences("vl", 0).getInt("by", (int) (8 * getResources().getDisplayMetrics().density));
        mWm.addView(mBubble, mBubbleLp);
        sendBarRect();
        try {
            mSounds = new SoundPool.Builder().setMaxStreams(3).setAudioAttributes(new AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_GAME).setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION).build()).build();
            mClickId = mSounds.load(getAssets().openFd("global_click.wav"), 1);
        } catch (Exception e) { mSounds = null; }
    }
    private int barW() { return pillPx() * PILLS + gapPx() * (PILLS - 1); }
    private void sendBarRect() {
        if (mGl == null || mBubbleLp == null) return;
        final String r = mBubbleLp.x + " " + mBubbleLp.y + " " + mBubbleLp.width + " " + mBubbleLp.height;
        mGl.queueEvent(() -> Native.setPref("bar", r));
    }
    void click() { if (mSounds != null && mClickId != 0 && mClickVol > 0) mSounds.play(mClickId, mClickVol, mClickVol, 1, 0, 1f); }

    final class BarView extends View {
        private final Paint mFill = new Paint(Paint.ANTI_ALIAS_FLAG), mFg = new Paint(Paint.ANTI_ALIAS_FLAG), mRing = new Paint(Paint.ANTI_ALIAS_FLAG);
        private Bitmap mChat;
        private float mDownX, mDownY; private int mStartX, mStartY; private boolean mMoved, mLong; private int mHeld = -1;
        private final Runnable mLongPress = () -> {
            if (mHeld != PILL_MIC) return;
            mLong = true; performHapticFeedback(0); click(); openFromBubble(100 + TAB_VOICE);
        };
        BarView(Context c) {
            super(c);
            mRing.setStyle(Paint.Style.STROKE); mRing.setColor(0xFF3CDC5A);
            try { mChat = BitmapFactory.decodeStream(c.getAssets().open("chat_icon.png")); } catch (Exception e) { mChat = null; }
        }
        private RectF pill(int i) {
            float p = pillPx(), g = gapPx(), x = i * (p + g);
            return new RectF(x, 0, x + p, p);
        }
        @Override protected void onDraw(Canvas c) {
            for (int i = 0; i < PILLS; ++i) {
                RectF r = pill(i);
                boolean on = i == PILL_MIC && mMic >= 2;
                int bg = on ? 0xEBEBEBEB : 0x87000000;   // UI::kPill / kPillActive
                if (i == mHeld && !mMoved) bg = on ? 0xEBDCDCDC : 0xAA282828;
                mFill.setColor(bg);
                c.drawRoundRect(r, r.height() * 0.26f, r.height() * 0.26f, mFill);
                int fg = on ? 0xFF0A0A0A : 0xFFFFFFFF;
                mFg.setColor(fg); mFg.setStyle(Paint.Style.FILL); mFg.setColorFilter(null);
                float cx = r.centerX(), cy = r.centerY(), h = r.height();
                switch (i) {
                    case PILL_DOTS: {
                        float rr = h * 0.066f, gap = h * 0.17f;
                        for (int k = -1; k <= 1; ++k) c.drawCircle(cx, cy + k * gap, rr, mFg);
                        break;
                    }
                    case PILL_CHAT: {
                        float s = h * 0.62f;
                        if (mChat != null) {
                            mFg.setColorFilter(new PorterDuffColorFilter(fg, PorterDuff.Mode.SRC_IN));
                            c.drawBitmap(mChat, null, new RectF(cx - s / 2, cy - s / 2, cx + s / 2, cy + s / 2), mFg);
                            mFg.setColorFilter(null);
                        } else {
                            RectF b = new RectF(cx - s * 0.5f, cy - s * 0.42f, cx + s * 0.5f, cy + s * 0.26f);
                            c.drawRoundRect(b, s * 0.22f, s * 0.22f, mFg);
                            Path t = new Path(); t.moveTo(cx - s * 0.30f, b.bottom - 1); t.lineTo(cx - s * 0.02f, b.bottom - 1); t.lineTo(cx - s * 0.36f, cy + s * 0.48f); t.close();
                            c.drawPath(t, mFg);
                        }
                        break;
                    }
                    case PILL_MIC: {
                        float rr = h * 0.30f;
                        int col = mMic == 0 ? 0x6EFFFFFF : fg;
                        drawMic(c, cx, cy, rr, col, mMic <= 1);
                        if (mMic == 3) { mRing.setStrokeWidth(h * 0.06f); RectF o = new RectF(r); o.inset(-h * 0.03f, -h * 0.03f); c.drawRoundRect(o, h * 0.28f, h * 0.28f, mRing); }
                        break;
                    }
                    default: {   // MonetLoader: crescent moon + a small star
                        float s = h * 0.62f, R = s * 0.46f;
                        Path moon = new Path(); moon.addCircle(cx - s * 0.06f, cy + s * 0.02f, R, Path.Direction.CW);
                        Path cut = new Path(); cut.addCircle(cx - s * 0.06f + R * 0.52f, cy + s * 0.02f - R * 0.24f, R * 0.86f, Path.Direction.CW);
                        moon.op(cut, Path.Op.DIFFERENCE);
                        c.drawPath(moon, mFg);
                        float sx = cx + s * 0.30f, sy = cy - s * 0.26f, k = s * 0.13f, w = s * 0.035f;
                        Path st = new Path();
                        st.moveTo(sx, sy - k); st.lineTo(sx + w, sy); st.lineTo(sx, sy + k); st.lineTo(sx - w, sy); st.close();
                        st.moveTo(sx - k, sy); st.lineTo(sx, sy - w); st.lineTo(sx + k, sy); st.lineTo(sx, sy + w); st.close();
                        c.drawPath(st, mFg);
                        break;
                    }
                }
            }
        }
        private void drawMic(Canvas c, float cx, float cy, float r, int col, boolean slash) {
            Paint p = new Paint(Paint.ANTI_ALIAS_FLAG); p.setColor(col);
            float w = r * 0.42f, h = r * 0.62f;
            c.drawRoundRect(new RectF(cx - w, cy - r * 0.95f, cx + w, cy - r * 0.95f + h * 1.55f), w, w, p);
            p.setStyle(Paint.Style.STROKE); p.setStrokeWidth(r * 0.13f); p.setStrokeCap(Paint.Cap.ROUND);
            float ar = r * 0.66f;
            c.drawArc(new RectF(cx - ar, cy - r * 0.1f - ar, cx + ar, cy - r * 0.1f + ar), 8, 164, false, p);
            c.drawLine(cx, cy + r * 0.56f, cx, cy + r * 0.86f, p);
            c.drawLine(cx - r * 0.34f, cy + r * 0.88f, cx + r * 0.34f, cy + r * 0.88f, p);
            if (slash) c.drawLine(cx - r * 0.8f, cy - r * 0.85f, cx + r * 0.8f, cy + r * 0.85f, p);
        }
        private int hit(float x) {
            for (int i = 0; i < PILLS; ++i) { RectF r = pill(i); if (x >= r.left - gapPx() / 2f && x <= r.right + gapPx() / 2f) return i; }
            return -1;
        }
        @Override public boolean onTouchEvent(MotionEvent e) {
            switch (e.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    mDownX = e.getRawX(); mDownY = e.getRawY(); mStartX = mBubbleLp.x; mStartY = mBubbleLp.y; mMoved = false; mLong = false;
                    mHeld = hit(e.getX());
                    postDelayed(mLongPress, 550);
                    invalidate();
                    return true;
                case MotionEvent.ACTION_MOVE: {
                    float dx = e.getRawX() - mDownX, dy = e.getRawY() - mDownY;
                    if (!mMoved && dx * dx + dy * dy > 144) { mMoved = true; removeCallbacks(mLongPress); invalidate(); }
                    if (mMoved) { mBubbleLp.x = mStartX + (int) dx; mBubbleLp.y = mStartY + (int) dy; try { mWm.updateViewLayout(this, mBubbleLp); } catch (Exception ignored) {} }
                    return true;
                }
                case MotionEvent.ACTION_UP: {
                    removeCallbacks(mLongPress);
                    int p = mHeld; mHeld = -1; invalidate();
                    if (mMoved) { getSharedPreferences("vl", 0).edit().putInt("bx", mBubbleLp.x).putInt("by", mBubbleLp.y).apply(); sendBarRect(); return true; }
                    if (mLong || p < 0) return true;
                    click();
                    switch (p) {
                        case PILL_DOTS: openFromBubble(Native.MODE_MENU); break;
                        case PILL_CHAT: openFromBubble(Native.MODE_CHAT); break;
                        case PILL_MIC: if (mGl != null) mGl.queueEvent(() -> Native.setPref("mic", "")); break;
                        default: openFromBubble(100 + TAB_MONET); break;
                    }
                    return true;
                }
                case MotionEvent.ACTION_CANCEL:
                    removeCallbacks(mLongPress); mHeld = -1; invalidate();
                    return true;
            }
            return false;
        }
    }
    private void applyBarPrefs(String arg) {
        String[] f = arg.trim().split(" ");
        try {
            boolean show = !"0".equals(f[0]);
            float size = Float.parseFloat(f[1]); mClickVol = Float.parseFloat(f[2]);
            if (mBubble == null) return;
            if (size != mBarSize) {
                mBarSize = size; mBubbleLp.width = barW(); mBubbleLp.height = pillPx();
                try { mWm.updateViewLayout(mBubble, mBubbleLp); } catch (Exception ignored) {}
                mBubble.invalidate(); sendBarRect();
            }
            mBarShown = show;
            mBubble.setVisibility(show && (mMode == Native.MODE_BUBBLE || mMode == Native.MODE_MENU) ? View.VISIBLE : View.GONE);
        } catch (Exception ignored) {}
    }
    private boolean mBarShown = true;

    // ---------------------------------------------------------------- calls from the native side (GL thread)
    void onNative(final String what, final String arg) {
        mMain.post(() -> {
            switch (what) {
                case "copy": {
                    ClipboardManager cm = (ClipboardManager) getSystemService(CLIPBOARD_SERVICE);
                    if (cm != null) cm.setPrimaryClip(ClipData.newPlainText("MP", arg));
                    break;
                }
                case "paste":   // an overlay without focus can't read the clipboard on Android 10+: a 1-frame activity does
                    startActivity(new Intent(this, PasteActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_NO_ANIMATION));
                    break;
                case "launch": MainActivity.launchGame(this); break;
                case "export": Bundle.exportAsync(this); break;
                case "toast": Toast.makeText(this, arg, Toast.LENGTH_SHORT).show(); break;
                case "click": click(); break;   // global_click (a launcher button was pressed)
                case "url":
                    try { startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(arg)).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)); } catch (Exception ignored) {}
                    break;
                case "mic": try { mMic = Integer.parseInt(arg.trim()); } catch (Exception ignored) {} if (mBubble != null) mBubble.invalidate(); break;
                case "prefs": applyBarPrefs(arg); break;
                case "barreset":
                    if (mBubble != null) {
                        mBubbleLp.x = (getResources().getDisplayMetrics().widthPixels - barW()) / 2; mBubbleLp.y = (int) (8 * getResources().getDisplayMetrics().density);
                        try { mWm.updateViewLayout(mBubble, mBubbleLp); } catch (Exception ignored) {}
                        getSharedPreferences("vl", 0).edit().putInt("bx", mBubbleLp.x).putInt("by", mBubbleLp.y).apply(); sendBarRect();
                    }
                    break;
            }
        });
    }
    void deliverPaste(final String text) { if (mGl != null) mGl.queueEvent(() -> Native.paste(text)); }
}
