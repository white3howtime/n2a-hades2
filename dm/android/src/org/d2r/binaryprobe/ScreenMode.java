package org.d2r.binaryprobe;

import android.content.Context;
import android.content.SharedPreferences;

/** How the fixed 16:9 game image is placed on the phone screen; persisted per install. */
final class ScreenMode {
    private static final String PREFS="hades_screen",KEY="fill";
    /** The mode the running game was started with; the render size is fixed at start, so the layout follows this, not the preference. */
    static boolean active=true;
    private ScreenMode(){}
    /** System-info overlay (FPS / CPU / GPU / memory) in the top-left corner; off by default. */
    static boolean info(Context c){return c.getSharedPreferences(PREFS,Context.MODE_PRIVATE).getBoolean("info",false);}
    /** On-screen buttons when idle: 0 stay, 1 fade, 2 hide (the settings gear only fades). Fade delay is fadeSecs (1-10). */
    static int fadeMode(Context c){android.content.SharedPreferences p=c.getSharedPreferences(PREFS,Context.MODE_PRIVATE);return p.getInt("fade_mode",p.getBoolean("fade",true)?1:0);}
    static int fadeSecs(Context c){return c.getSharedPreferences(PREFS,Context.MODE_PRIVATE).getInt("fade_s",1);}
    static void setFade(Context c,int mode,int secs){c.getSharedPreferences(PREFS,Context.MODE_PRIVATE).edit().putInt("fade_mode",mode).putInt("fade_s",secs).apply();
        org.hades2.mobile.HadesPadView.setFade(mode,secs*1000L);}
    static void applyFade(Context c){org.hades2.mobile.HadesPadView.setFade(fadeMode(c),fadeSecs(c)*1000L);}
    static void setInfo(Context c,boolean on){c.getSharedPreferences(PREFS,Context.MODE_PRIVATE).edit().putBoolean("info",on).apply();}
    /** true: stretch to the whole screen (default; the picture is widened on 19.5:9-type panels). false: keep 16:9 with black bars. */
    static boolean fill(Context c){return c.getSharedPreferences(PREFS,Context.MODE_PRIVATE).getBoolean(KEY,true);}
    static void setFill(Context c,boolean fill){
        SharedPreferences.Editor e=c.getSharedPreferences(PREFS,Context.MODE_PRIVATE).edit();e.putBoolean(KEY,fill).apply();
    }
}
