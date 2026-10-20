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
        float dp = getResources().getDisplayMetrics().density;
        float x = getWidth() / 2f, y = getHeight() / 2f, r = Math.min(x, y) - dp * 1.5f;
        fill.setShader(null); fill.setColor(0x33000000); c.drawCircle(x, y + dp * 1.5f, r + dp, fill);
        fill.setShader(new android.graphics.LinearGradient(x, y - r, x, y + r, 0x9A3A4452, 0x9A12161C, android.graphics.Shader.TileMode.CLAMP));
        c.drawCircle(x, y, r, fill); fill.setShader(null);
        line.setColor(0x80FFFFFF); line.setStrokeWidth(1.5f * dp); c.drawCircle(x, y, r, line);
        float k = r * .5f;
        fill.setColor(0xF2FFFFFF); line.setColor(0xF2FFFFFF); line.setStrokeWidth(k * .42f); line.setStrokeCap(Paint.Cap.BUTT);
        for (int i = 0; i < 8; i++) {   // teeth
            double a = i * Math.PI / 4;
            float cs = (float) Math.cos(a), sn = (float) Math.sin(a);
            c.drawLine(x + cs * k * .9f, y + sn * k * .9f, x + cs * k * 1.45f, y + sn * k * 1.45f, line);
        }
        android.graphics.Path p = new android.graphics.Path(); p.setFillType(android.graphics.Path.FillType.EVEN_ODD);
        p.addCircle(x, y, k * 1.05f, android.graphics.Path.Direction.CW); p.addCircle(x, y, k * .42f, android.graphics.Path.Direction.CCW);
        c.drawPath(p, fill);
    }
}
