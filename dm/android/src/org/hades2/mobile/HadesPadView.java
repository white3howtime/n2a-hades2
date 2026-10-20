package org.hades2.mobile;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowInsets;
import java.util.Locale;
import java.util.function.BooleanSupplier;

/**
 * On-screen gamepad for Hades II: floating stick on the left, action buttons on the right.
 * Draws and forwards touches; all logic lives in hades_pad.c (tested on the host).
 *
 * NOT compiled or run yet (no Android SDK was available when this was written).
 */
public final class HadesPadView extends View {
    /** Receives the Npad state; wire it to GameActivity.nativeActions (dm_android_input_publish_actions). */
    public interface Sink { void publish(long buttons, int lx, int ly, int rx, int ry); }

    private static native void nativeResize(float w, float h, float density, float il, float ir, float ib);
    private static native void nativeTouch(int[] ids, float[] xs, float[] ys, boolean cancelled, long[] out);
    private static native void nativeSetMode(int mode);
    private static native int nativeMode();
    private static native int nativeConfig(String text);
    private static native String nativeLabel(int btn, int lang);
    private static native int nativeVisual(float[] out);

    // Must match hp_btn in hades_pad.h.
    private static final int HP_MODE = 9;
    private static final int VIS_FLOATS = 1 + 5 * 20 + 12;

    private final BooleanSupplier ready;
    private final Sink sink;
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG), line = new Paint(Paint.ANTI_ALIAS_FLAG), text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Rect bounds = new Rect();
    private final long[] out = new long[5];
    private final float[] vis = new float[VIS_FLOATS];
    private final int[] ids = new int[10];
    private final float[] xs = new float[10], ys = new float[10];
    private final int lang;
    private boolean active = true;
    private long lastButtons = -1;
    private int lastLx, lastLy, lastRx, lastRy;

    public HadesPadView(Context context, BooleanSupplier ready, Sink sink) {
        super(context);
        this.ready = ready; this.sink = sink;
        lang = Locale.getDefault().getLanguage().startsWith("zh") ? 1 : 0;
        setClickable(false); setFocusable(false);
        line.setStyle(Paint.Style.STROKE); text.setTextAlign(Paint.Align.CENTER);
    }

    /** Extra "NAME=KEY[+KEY]" lines, see hp_config_parse. Returns lines applied, or minus the first bad line. */
    public static int configure(String mapping) { return nativeConfig(mapping); }

    public void setActive(boolean on) { active = on; if (!on) cancel(); setVisibility(on ? VISIBLE : GONE); }

    /** Release everything (pause, dialog, mode change). */
    public void cancel() {
        nativeTouch(ids, xs, ys, true, out);   // cancelled: the native side drops every pointer
        publish(true); invalidate();
    }

    @Override protected void onSizeChanged(int w, int h, int ow, int oh) {
        float il = 0, ir = 0, ib = 0;
        WindowInsets in = getRootWindowInsets();
        if (in != null) { il = in.getSystemWindowInsetLeft(); ir = in.getSystemWindowInsetRight(); ib = in.getSystemWindowInsetBottom(); }
        nativeResize(w, h, getResources().getDisplayMetrics().density, il, ir, ib);
        publish(true);
    }

    @Override public boolean onTouchEvent(MotionEvent e) {
        if (!active || !ready.getAsBoolean()) { if (lastButtons != 0) cancel(); return false; }
        int action = e.getActionMasked();
        boolean cancelled = action == MotionEvent.ACTION_CANCEL;
        int skip = action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP ? e.getActionIndex() : -1;
        int n = 0;
        for (int i = 0; i < e.getPointerCount() && n < ids.length; i++) {
            if (i == skip) continue;
            ids[n] = e.getPointerId(i); xs[n] = e.getX(i); ys[n] = e.getY(i); n++;
        }
        int[] idv = java.util.Arrays.copyOf(ids, n); float[] xv = java.util.Arrays.copyOf(xs, n), yv = java.util.Arrays.copyOf(ys, n);
        long before = lastButtons;
        nativeTouch(idv, xv, yv, cancelled, out);
        publish(false);
        if ((out[0] & ~before) != 0 && before >= 0) performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP);
        invalidate();
        return true;
    }

    private void publish(boolean force) {
        long b = out[0]; int lx = (int) out[1], ly = (int) out[2], rx = (int) out[3], ry = (int) out[4];
        if (!force && b == lastButtons && lx == lastLx && ly == lastLy && rx == lastRx && ry == lastRy) return;
        lastButtons = b; lastLx = lx; lastLy = ly; lastRx = rx; lastRy = ry;
        sink.publish(b, lx, ly, rx, ry);
    }

    @Override protected void onDraw(Canvas c) {
        if (!active || nativeVisual(vis) < 0) return;
        int n = (int) vis[0], t = 1 + 5 * 20;
        float dp = getResources().getDisplayMetrics().density;
        boolean menu = vis[t + 11] != 0;
        for (int i = 0; i < n; i++) {
            int btn = (int) vis[1 + 5 * i]; float cx = vis[2 + 5 * i], cy = vis[3 + 5 * i], r = vis[4 + 5 * i];
            boolean pressed = vis[5 + 5 * i] != 0;
            fill.setColor(pressed ? 0x99FFFFFF : 0x40000000); c.drawCircle(cx, cy, r, fill);
            line.setStrokeWidth(2 * dp); line.setColor(0x99FFFFFF); c.drawCircle(cx, cy, r, line);
            String s = btn == HP_MODE ? (menu ? (lang == 1 ? "游戏" : "Play") : nativeLabel(btn, lang)) : nativeLabel(btn, lang);
            text.setColor(pressed ? 0xFF000000 : 0xFFFFFFFF);
            text.setTextSize(Math.min(r * .5f, 18 * dp));
            text.getTextBounds(s, 0, s.length(), bounds);
            c.drawText(s, cx, cy - bounds.exactCenterY(), text);
        }
        if (vis[t] != 0) {
            float r = vis[t + 5];
            fill.setColor(0x30FFFFFF); c.drawCircle(vis[t + 1], vis[t + 2], r, fill);
            line.setColor(0x80FFFFFF); c.drawCircle(vis[t + 1], vis[t + 2], r, line);
            fill.setColor(0xB0FFFFFF); c.drawCircle(vis[t + 3], vis[t + 4], r * .45f, fill);
        }
        if (vis[t + 6] != 0) {
            line.setColor(0xCCFFFFFF); c.drawLine(vis[t + 7], vis[t + 8], vis[t + 9], vis[t + 10], line);
        }
    }
}
