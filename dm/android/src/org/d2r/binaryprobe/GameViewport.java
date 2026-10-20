package org.d2r.binaryprobe;

import android.content.Context;
import android.view.View;
import android.widget.FrameLayout;

/** Places the game image and every input surface in one rectangle: the whole screen, or the largest 16:9 rectangle (see ScreenMode). */
final class GameViewport extends FrameLayout {
    GameViewport(Context context){super(context);}
    @Override protected void onMeasure(int widthSpec,int heightSpec) {
        int w=View.MeasureSpec.getSize(widthSpec),h=View.MeasureSpec.getSize(heightSpec);
        int[] size=ScreenMode.fill(getContext())?new int[]{Math.max(1,w),Math.max(1,h)}:GameDisplayMetrics.fitGame(w,h);
        super.onMeasure(View.MeasureSpec.makeMeasureSpec(size[0],View.MeasureSpec.EXACTLY),
                View.MeasureSpec.makeMeasureSpec(size[1],View.MeasureSpec.EXACTLY));
    }
}
