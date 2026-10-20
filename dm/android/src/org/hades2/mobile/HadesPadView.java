package org.hades2.mobile;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
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
            fill.setColor(pressed ? 0x99FFFFFF : 0x26000000); c.drawCircle(cx, cy, r, fill);
            line.setStrokeWidth(1.5f * dp); line.setColor(pressed ? 0xCCFFFFFF : 0x66FFFFFF); c.drawCircle(cx, cy, r, line);
            line.setColor(pressed ? 0xFF000000 : 0xE0FFFFFF);
            icon(c, btn, cx, cy, r, menu);
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

    /** Vector icons instead of text: keeps the buttons small and language-free. Order matches hp_btn in hades_pad.h. */
    private void icon(Canvas c, int btn, float x, float y, float r, boolean menu) {
        float k = r * .5f, dp = getResources().getDisplayMetrics().density;
        line.setStrokeWidth(Math.max(2f * dp, r * .12f)); line.setStrokeCap(Paint.Cap.ROUND); line.setStrokeJoin(Paint.Join.ROUND);
        line.setStyle(Paint.Style.STROKE);
        Path p = new Path();
        switch (btn) {
            case 0: // attack: sword
                c.drawLine(x - k, y + k, x + k, y - k, line);
                c.drawLine(x - k * .15f, y - k * .15f - k * .45f, x + k * .45f + k * .15f, y + k * .15f, line);
                c.drawLine(x - k * .95f, y + k * .95f, x - k * 1.15f, y + k * 1.15f, line); break;
            case 1: // special: four-point star
                p.moveTo(x, y - k * 1.2f); p.quadTo(x, y, x + k * 1.2f, y); p.quadTo(x, y, x, y + k * 1.2f);
                p.quadTo(x, y, x - k * 1.2f, y); p.quadTo(x, y, x, y - k * 1.2f); c.drawPath(p, line); break;
            case 2: // cast: ring with a dot
                c.drawCircle(x, y, k, line); c.drawCircle(x, y, k * .25f, line); break;
            case 3: // dash: double chevron
                for (int i = 0; i < 2; i++) { float o = (i - .5f) * k * 1.1f; p.moveTo(x + o - k * .45f, y - k * .7f); p.lineTo(x + o + k * .35f, y); p.lineTo(x + o - k * .45f, y + k * .7f); }
                c.drawPath(p, line); break;
            case 4: // interact: speech bubble with dots
                c.drawRoundRect(x - k * 1.1f, y - k * .8f, x + k * 1.1f, y + k * .6f, k * .4f, k * .4f, line);
                for (int i = -1; i <= 1; i++) c.drawPoint(x + i * k * .5f, y - k * .1f, line);
                break;
            case 5: // call: horn
                p.moveTo(x - k, y - k * .3f); p.lineTo(x - k * .3f, y - k * .3f); p.lineTo(x + k * .6f, y - k * .9f); p.lineTo(x + k * .6f, y + k * .9f);
                p.lineTo(x - k * .3f, y + k * .3f); p.lineTo(x - k, y + k * .3f); p.close(); c.drawPath(p, line); break;
            case 6: // lock: crosshair
                c.drawCircle(x, y, k * .6f, line);
                c.drawLine(x, y - k * 1.1f, x, y - k * .6f, line); c.drawLine(x, y + k * .6f, x, y + k * 1.1f, line);
                c.drawLine(x - k * 1.1f, y, x - k * .6f, y, line); c.drawLine(x + k * .6f, y, x + k * 1.1f, y, line); break;
            case 7: // pause: two bars
                c.drawLine(x - k * .4f, y - k * .7f, x - k * .4f, y + k * .7f, line); c.drawLine(x + k * .4f, y - k * .7f, x + k * .4f, y + k * .7f, line); break;
            case 8: // info: i
                c.drawPoint(x, y - k * .8f, line); c.drawLine(x, y - k * .2f, x, y + k * .8f, line); break;
            case 9: // menu: three lines (play triangle when already in the menu)
                if (menu) { p.moveTo(x - k * .5f, y - k * .8f); p.lineTo(x + k * .8f, y); p.lineTo(x - k * .5f, y + k * .8f); p.close(); c.drawPath(p, line); }
                else for (int i = -1; i <= 1; i++) c.drawLine(x - k * .8f, y + i * k * .6f, x + k * .8f, y + i * k * .6f, line);
                break;
            case 10: arrow(c, x, y, k, 0, -1); break;
            case 11: arrow(c, x, y, k, 0, 1); break;
            case 12: arrow(c, x, y, k, -1, 0); break;
            case 13: arrow(c, x, y, k, 1, 0); break;
            case 14: // confirm: check
                p.moveTo(x - k * .8f, y); p.lineTo(x - k * .2f, y + k * .6f); p.lineTo(x + k * .9f, y - k * .6f); c.drawPath(p, line); break;
            case 15: // back: left arrow with a tail
                c.drawLine(x + k * .8f, y, x - k * .8f, y, line); c.drawLine(x - k * .8f, y, x - k * .2f, y - k * .6f, line); c.drawLine(x - k * .8f, y, x - k * .2f, y + k * .6f, line); break;
            case 16: case 17: // option 1/2: one or two dots
                if (btn == 16) c.drawPoint(x, y, line); else { c.drawPoint(x - k * .4f, y, line); c.drawPoint(x + k * .4f, y, line); } break;
            case 18: case 19: { // page: double chevron
                float d = btn == 18 ? -1 : 1;
                for (int i = 0; i < 2; i++) { float o = (i - .5f) * k * .9f * d; p.moveTo(x + o - d * k * .3f, y - k * .6f); p.lineTo(x + o + d * k * .3f, y); p.lineTo(x + o - d * k * .3f, y + k * .6f); }
                c.drawPath(p, line); break; }
        }
    }

    private void arrow(Canvas c, float x, float y, float k, float dx, float dy) {
        Path p = new Path(); float px = -dy, py = dx;   // perpendicular
        p.moveTo(x + dx * k * .7f, y + dy * k * .7f);
        p.lineTo(x - dx * k * .5f + px * k * .8f, y - dy * k * .5f + py * k * .8f);
        p.lineTo(x - dx * k * .5f - px * k * .8f, y - dy * k * .5f - py * k * .8f); p.close();
        fill.setColor(line.getColor()); c.drawPath(p, fill);
    }
}
