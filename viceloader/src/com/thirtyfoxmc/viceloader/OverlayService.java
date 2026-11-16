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
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.graphics.RectF;
import android.graphics.Typeface;
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
 * The "display over other apps" part of VICE Loader: a small floating bubble (the San Andreas MP floating menu
 * button) and, while the launcher or the chat keyboard is open, a full screen translucent GLES3 window with the
 * ImGui launcher over GTA Vice City. While only the bubble shows, the big window is invisible and lets every touch
 * through to the game.
 */
public class OverlayService extends Service {
    static volatile OverlayService sInstance;
    static final String CH = "viceloader";
    static final String ACT_STOP = "com.thirtyfoxmc.viceloader.STOP", ACT_PANEL = "com.thirtyfoxmc.viceloader.PANEL", ACT_CHAT = "com.thirtyfoxmc.viceloader.CHAT";

    private final Handler mMain = new Handler(Looper.getMainLooper());
    private WindowManager mWm;
    private GLSurfaceView mGl;
    private WindowManager.LayoutParams mGlLp, mBubbleLp;
    private BubbleView mBubble;
    private volatile int mMode = -1;
    private boolean mGlTouchable = true;

    public static boolean canDraw(Context c) { return Build.VERSION.SDK_INT < 23 || Settings.canDrawOverlays(c); }
    @Override public IBinder onBind(Intent i) { return null; }

    @Override public void onCreate() {
        super.onCreate();
        sInstance = this;
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
        return START_STICKY;
    }

    @Override public void onDestroy() {
        mMain.removeCallbacks(mTick);
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
        b.setSmallIcon(R.drawable.icon).setContentTitle("VICE Loader").setContentText("MP launcher over GTA Vice City - tap the bubble")
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
        if (mBubble != null) mBubble.setVisibility(big ? View.GONE : View.VISIBLE);
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
        setMode(mode);
        mGl.requestRender();
    }

    // ---------------------------------------------------------------- the floating bubble
    private void addBubble() {
        mBubble = new BubbleView(this);
        int sz = (int) (56 * getResources().getDisplayMetrics().density);
        mBubbleLp = new WindowManager.LayoutParams(sz, sz, overlayType(),
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN, PixelFormat.TRANSLUCENT);
        mBubbleLp.gravity = Gravity.TOP | Gravity.LEFT;
        mBubbleLp.x = getSharedPreferences("vl", 0).getInt("bx", 24);
        mBubbleLp.y = getSharedPreferences("vl", 0).getInt("by", 160);
        mWm.addView(mBubble, mBubbleLp);
    }

    final class BubbleView extends View {
        private final Paint mFill = new Paint(Paint.ANTI_ALIAS_FLAG), mLine = new Paint(Paint.ANTI_ALIAS_FLAG), mText = new Paint(Paint.ANTI_ALIAS_FLAG);
        private float mDownX, mDownY; private int mStartX, mStartY; private boolean mMoved, mLong;
        private final Runnable mLongPress = () -> { mLong = true; performHapticFeedback(0); openFromBubble(Native.MODE_CHAT); };
        BubbleView(Context c) {
            super(c);
            mFill.setColor(0x87000000);
            mLine.setColor(0xF0FFFFFF); mLine.setStyle(Paint.Style.STROKE);
            mText.setColor(0xFFFFFFFF); mText.setTextAlign(Paint.Align.CENTER); mText.setTypeface(Typeface.DEFAULT_BOLD);
        }
        @Override protected void onDraw(Canvas c) {
            float w = getWidth(), h = getHeight(), d = getResources().getDisplayMetrics().density;
            RectF r = new RectF(d * 2, d * 2, w - d * 2, h - d * 2);
            c.drawRoundRect(r, d * 12, d * 12, mFill);   // the TopbarPlus pill of San Andreas MP
            mLine.setStrokeWidth(d * 1.5f);
            c.drawRoundRect(r, d * 12, d * 12, mLine);
            mText.setTextSize(h * 0.30f);
            c.drawText("VL", w / 2, h / 2 + h * 0.03f, mText);
            mText.setTextSize(h * 0.15f);
            c.drawText("MP", w / 2, h / 2 + h * 0.26f, mText);
        }
        @Override public boolean onTouchEvent(MotionEvent e) {
            switch (e.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    mDownX = e.getRawX(); mDownY = e.getRawY(); mStartX = mBubbleLp.x; mStartY = mBubbleLp.y; mMoved = false; mLong = false;
                    postDelayed(mLongPress, 550);
                    return true;
                case MotionEvent.ACTION_MOVE: {
                    float dx = e.getRawX() - mDownX, dy = e.getRawY() - mDownY;
                    if (!mMoved && dx * dx + dy * dy > 144) { mMoved = true; removeCallbacks(mLongPress); }
                    if (mMoved) { mBubbleLp.x = mStartX + (int) dx; mBubbleLp.y = mStartY + (int) dy; try { mWm.updateViewLayout(this, mBubbleLp); } catch (Exception ignored) {} }
                    return true;
                }
                case MotionEvent.ACTION_UP:
                    removeCallbacks(mLongPress);
                    if (mMoved) getSharedPreferences("vl", 0).edit().putInt("bx", mBubbleLp.x).putInt("by", mBubbleLp.y).apply();
                    else if (!mLong) openFromBubble(Native.MODE_PANEL);
                    return true;
                case MotionEvent.ACTION_CANCEL:
                    removeCallbacks(mLongPress);
                    return true;
            }
            return false;
        }
    }

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
            }
        });
    }
    void deliverPaste(final String text) { if (mGl != null) mGl.queueEvent(() -> Native.paste(text)); }
}
