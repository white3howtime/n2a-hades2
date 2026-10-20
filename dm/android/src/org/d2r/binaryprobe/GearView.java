package org.d2r.binaryprobe;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.view.View;

/** Round gear icon button for the system menu; drawn like the touch-pad buttons so it matches them. */
final class GearView extends View {
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG), line = new Paint(Paint.ANTI_ALIAS_FLAG);

    GearView(Context c) {
        super(c);
        float dp = c.getResources().getDisplayMetrics().density;
        fill.setColor(0x26000000);
        line.setStyle(Paint.Style.STROKE); line.setStrokeCap(Paint.Cap.ROUND); line.setStrokeWidth(1.5f * dp);
        setClickable(true); setFocusable(true);
    }

    @Override protected void onDraw(Canvas c) {
        float x = getWidth() / 2f, y = getHeight() / 2f, r = Math.min(x, y) - 1;
        c.drawCircle(x, y, r, fill);
        line.setColor(0x66FFFFFF); c.drawCircle(x, y, r, line);
        line.setColor(0xE0FFFFFF); line.setStrokeWidth(Math.max(2f * getResources().getDisplayMetrics().density, r * .12f));
        float k = r * .5f;
        c.drawCircle(x, y, k * .8f, line);
        for (int i = 0; i < 8; i++) {   // teeth
            double a = i * Math.PI / 4;
            float cs = (float) Math.cos(a), sn = (float) Math.sin(a);
            c.drawLine(x + cs * k * 1.05f, y + sn * k * 1.05f, x + cs * k * 1.4f, y + sn * k * 1.4f, line);
        }
        c.drawCircle(x, y, k * .22f, line);
    }
}
