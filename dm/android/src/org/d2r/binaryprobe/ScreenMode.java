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
    static void setInfo(Context c,boolean on){c.getSharedPreferences(PREFS,Context.MODE_PRIVATE).edit().putBoolean("info",on).apply();}
    /** true: stretch to the whole screen (default; the picture is widened on 19.5:9-type panels). false: keep 16:9 with black bars. */
    static boolean fill(Context c){return c.getSharedPreferences(PREFS,Context.MODE_PRIVATE).getBoolean(KEY,true);}
    static void setFill(Context c,boolean fill){
        SharedPreferences.Editor e=c.getSharedPreferences(PREFS,Context.MODE_PRIVATE).edit();e.putBoolean(KEY,fill).apply();
    }
}
