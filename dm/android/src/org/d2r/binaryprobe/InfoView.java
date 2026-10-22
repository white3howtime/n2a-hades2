package org.d2r.binaryprobe;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.util.TypedValue;
import android.view.View;

/** System-info overlay: each line is "name\tvalue", names flush left and values flush right. */
final class InfoView extends View {
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final float gap, pad;
    private String[][] rows = new String[0][];
    private String text = "";

    InfoView(Context c) {
        super(c);
        float dp = c.getResources().getDisplayMetrics().density;
        paint.setColor(TouchControlsView.TEXT);
        paint.setTextSize(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP, 11, c.getResources().getDisplayMetrics()));
        gap = 16 * dp; pad = 8 * dp;
        setBackgroundColor(0x99000000);
    }

    String getText() { return text; }

    void setText(String s) {
        text = s; String[] lines = s.split("\n"); rows = new String[lines.length][];
        for (int i = 0; i < lines.length; i++) rows[i] = lines[i].split("\t", 2);
        requestLayout(); invalidate();
    }

    private float line() { Paint.FontMetrics m = paint.getFontMetrics(); return (m.descent - m.ascent) * 1.1f; }

    @Override protected void onMeasure(int w, int h) {
        float name = 0, value = 0;
        for (String[] r : rows) { name = Math.max(name, paint.measureText(r[0])); if (r.length > 1) value = Math.max(value, paint.measureText(r[1])); }
        setMeasuredDimension((int) (name + gap + value + 2 * pad), (int) (rows.length * line() + pad));
    }

    @Override protected void onDraw(Canvas c) {
        float y = pad / 2 - paint.getFontMetrics().ascent, step = line();
        for (String[] r : rows) {
            paint.setTextAlign(Paint.Align.LEFT); c.drawText(r[0], pad, y, paint);
            if (r.length > 1) { paint.setTextAlign(Paint.Align.RIGHT); c.drawText(r[1], getWidth() - pad, y, paint); }
            y += step;
        }
    }
}
