/* JNI glue between org.hades2.mobile.HadesPadView and hades_pad.c.
 *
 * NOT compiled or run yet: it was written without an Android NDK on hand. The core in hades_pad.c is tested
 * on the host; this file only moves arrays across the JNI boundary.
 *
 * All calls come from the Android UI thread, so the state needs no lock.
 * Publishing to the game stays in Java (GameActivity.nativeActions -> dm_android_input_publish_actions),
 * so this file does not depend on dodola's headers.
 */
#include <jni.h>
#include <string.h>
#include "hades_pad.h"

#define VIS_FLOATS (1 + 5 * HP_NBTN + 12)

/* HadesPadView.java hard-codes these. */
_Static_assert(HP_MODE == 9 && HP_NBTN == 20, "update HadesPadView.java");

static hp_state g_pad;
static int g_inited;

static void ensure(void) { if (!g_inited) { hp_init(&g_pad, NULL); g_inited = 1; } }

JNIEXPORT void JNICALL Java_org_hades2_mobile_HadesPadView_nativeResize(JNIEnv *e, jclass c, jfloat w, jfloat h, jfloat density,
                                                                       jfloat il, jfloat ir, jfloat ib) {
    (void)e; (void)c; ensure(); hp_resize(&g_pad, w, h, density, il, ir, ib);
}

/* out: long[5] = buttons, lx, ly, rx, ry. ids/xs/ys have the same length (all pointers currently down). */
JNIEXPORT void JNICALL Java_org_hades2_mobile_HadesPadView_nativeTouch(JNIEnv *e, jclass c, jintArray ids, jfloatArray xs,
                                                                      jfloatArray ys, jboolean cancelled, jlongArray out) {
    (void)c; ensure();
    jsize n = (*e)->GetArrayLength(e, ids);
    if (n > HP_MAX_POINTERS) n = HP_MAX_POINTERS;
    jint ib[HP_MAX_POINTERS]; jfloat xb[HP_MAX_POINTERS], yb[HP_MAX_POINTERS];
    (*e)->GetIntArrayRegion(e, ids, 0, n, ib);
    (*e)->GetFloatArrayRegion(e, xs, 0, n, xb);
    (*e)->GetFloatArrayRegion(e, ys, 0, n, yb);
    int idi[HP_MAX_POINTERS]; for (int i = 0; i < n; i++) idi[i] = ib[i];
    hp_touch(&g_pad, idi, xb, yb, n, cancelled ? 1 : 0);
    hp_out o; hp_output(&g_pad, &o);
    jlong r[5] = { (jlong)o.buttons, o.lx, o.ly, o.rx, o.ry };
    (*e)->SetLongArrayRegion(e, out, 0, 5, r);
}

JNIEXPORT void JNICALL Java_org_hades2_mobile_HadesPadView_nativeSetMode(JNIEnv *e, jclass c, jint mode) {
    (void)e; (void)c; ensure(); hp_set_mode(&g_pad, mode ? HP_MODE_MENU : HP_MODE_PLAY);
}
JNIEXPORT jint JNICALL Java_org_hades2_mobile_HadesPadView_nativeMode(JNIEnv *e, jclass c) {
    (void)e; (void)c; ensure(); return g_pad.mode;
}

/* Returns lines applied, or minus the first bad line number (see hp_config_parse). */
JNIEXPORT jint JNICALL Java_org_hades2_mobile_HadesPadView_nativeConfig(JNIEnv *e, jclass c, jstring text) {
    (void)c; ensure();
    const char *t = (*e)->GetStringUTFChars(e, text, NULL);
    if (!t) return 0;
    int r = hp_config_parse(&g_pad.cfg, t);
    (*e)->ReleaseStringUTFChars(e, text, t);
    return r;
}

JNIEXPORT jstring JNICALL Java_org_hades2_mobile_HadesPadView_nativeLabel(JNIEnv *e, jclass c, jint btn, jint lang) {
    (void)c; return (*e)->NewStringUTF(e, hp_label((hp_btn)btn, lang));
}

/* out (>= VIS_FLOATS floats): [0]=n, then n * {btn, cx, cy, r, pressed}, then at 1+5*HP_NBTN:
 * stick_active, base_x, base_y, knob_x, knob_y, stick_r, aim_active, aim_x0, aim_y0, aim_x1, aim_y1, mode. */
JNIEXPORT jint JNICALL Java_org_hades2_mobile_HadesPadView_nativeVisual(JNIEnv *e, jclass c, jfloatArray out) {
    (void)c; ensure();
    if ((*e)->GetArrayLength(e, out) < VIS_FLOATS) return -1;
    hp_vis v; hp_visual(&g_pad, &v);
    float f[VIS_FLOATS]; memset(f, 0, sizeof f);
    f[0] = (float)v.nbtns;
    for (int i = 0; i < v.nbtns; i++) {
        f[1 + 5 * i] = (float)v.btns[i].btn; f[2 + 5 * i] = v.btns[i].cx; f[3 + 5 * i] = v.btns[i].cy;
        f[4 + 5 * i] = v.btns[i].r; f[5 + 5 * i] = (float)v.btns[i].pressed;
    }
    float *t = f + 1 + 5 * HP_NBTN;
    t[0] = (float)v.stick_active; t[1] = v.base_x; t[2] = v.base_y; t[3] = v.knob_x; t[4] = v.knob_y; t[5] = v.stick_r;
    t[6] = (float)v.aim_active; t[7] = v.aim_x0; t[8] = v.aim_y0; t[9] = v.aim_x1; t[10] = v.aim_y1; t[11] = (float)g_pad.mode;
    (*e)->SetFloatArrayRegion(e, out, 0, VIS_FLOATS, f);
    return v.nbtns;
}
