/* Virtual gamepad for the Hades II Android port.
 *
 * Platform-free: no Android, JNI or dodola dependencies, so it builds and tests on the host.
 * The caller feeds it the full list of active touch pointers (what an Android MotionEvent gives)
 * and gets back an Npad state (button mask + two sticks) to hand to dm_android_input_publish_actions.
 *
 * Coordinates are view pixels, y grows downwards. Sizes in the layout table are dp, scaled by density.
 */
#ifndef HADES_PAD_H
#define HADES_PAD_H
#include <stdint.h>

/* Npad button bits; same values as DM_BTN_* in dodola's dm_input.h / nn::hid::NpadButton. */
enum {
    HP_NPAD_A = 1u << 0,  HP_NPAD_B = 1u << 1,  HP_NPAD_X = 1u << 2,  HP_NPAD_Y = 1u << 3,
    HP_NPAD_LS = 1u << 4, HP_NPAD_RS = 1u << 5, HP_NPAD_L = 1u << 6,  HP_NPAD_R = 1u << 7,
    HP_NPAD_ZL = 1u << 8, HP_NPAD_ZR = 1u << 9, HP_NPAD_PLUS = 1u << 10, HP_NPAD_MINUS = 1u << 11,
    HP_NPAD_LEFT = 1u << 12, HP_NPAD_UP = 1u << 13, HP_NPAD_RIGHT = 1u << 14, HP_NPAD_DOWN = 1u << 15
};

/* On-screen controls. Labels never use console key names. */
typedef enum {
    /* gameplay */
    HP_ATTACK, HP_SPECIAL, HP_CAST, HP_DASH, HP_INTERACT, HP_CALL, HP_LOCK,
    /* top bar, both modes */
    HP_PAUSE, HP_INFO, HP_MODE,
    /* menu mode */
    HP_DPAD_UP, HP_DPAD_DOWN, HP_DPAD_LEFT, HP_DPAD_RIGHT,
    HP_CONFIRM, HP_BACK, HP_ALT1, HP_ALT2, HP_PAGE_PREV, HP_PAGE_NEXT,
    HP_NBTN
} hp_btn;

typedef enum { HP_MODE_PLAY = 0, HP_MODE_MENU = 1 } hp_mode;

/* Which Npad bits each control presses. Defaults are a GUESS until checked in the game's controls screen. */
typedef struct { uint32_t mask[HP_NBTN]; } hp_config;

void hp_config_default(hp_config *c);
/* Lines "NAME=KEY[+KEY...]", e.g. "CAST=ZR" or "CALL=ZL+ZR". '#' starts a comment.
 * Returns the number of lines applied, or minus the 1-based number of the first malformed line.
 * Valid lines are applied even when another line is malformed. HP_MODE cannot be remapped. */
int hp_config_parse(hp_config *c, const char *text);

#define HP_MAX_POINTERS 10

typedef struct {
    hp_config cfg;
    float w, h, density;               /* view size in px, px per dp */
    float inset_l, inset_r, inset_b;   /* safe-area insets in px */
    hp_mode mode;
    struct { int id, role, btn; float ox, oy, x, y; } p[HP_MAX_POINTERS];
    int np;
    float sbx, sby;                    /* floating stick base, valid while a STICK pointer is down */
} hp_state;

typedef struct { uint64_t buttons; int32_t lx, ly, rx, ry; } hp_out;   /* sticks in [-32767, 32767], y up */

typedef struct { int btn; float cx, cy, r; int pressed; } hp_vis_btn;
typedef struct {
    hp_vis_btn btns[HP_NBTN]; int nbtns;
    int stick_active; float base_x, base_y, knob_x, knob_y, stick_r;
    int aim_active; float aim_x0, aim_y0, aim_x1, aim_y1;
} hp_vis;

void hp_init(hp_state *s, const hp_config *c);
void hp_resize(hp_state *s, float w, float h, float density, float inset_l, float inset_r, float inset_b);
void hp_set_mode(hp_state *s, hp_mode m);
/* ids/xs/ys: every pointer currently down (n may be 0). cancelled drops everything. */
void hp_touch(hp_state *s, const int *ids, const float *xs, const float *ys, int n, int cancelled);
void hp_output(const hp_state *s, hp_out *o);
void hp_visual(const hp_state *s, hp_vis *v);
/* label for a control; lang 0 = English, 1 = Chinese. Never returns console key names. */
const char *hp_label(hp_btn b, int lang);

#endif
