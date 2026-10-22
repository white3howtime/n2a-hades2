#include "hades_pad.h"
#include <math.h>
#include <string.h>
#include <ctype.h>

enum { ROLE_NONE = 0, ROLE_STICK, ROLE_BTN };
enum { M_PLAY = 1, M_MENU = 2, M_BOTH = 3 };
enum { AN_R, AN_L, AN_T, AN_C };   /* right-bottom cluster, left-bottom cluster, top-right corner, top centre */

/* Two layouts toggled by MODE: play (left half is a floating stick) and D-pad (four direction buttons instead of the stick).
 * Controls with modes = 0 are never shown or hit. Menus use Cast (A) to confirm and Dash (B) to go back. */
static const struct { const char *name; unsigned modes; int anchor; float dx, dy, r; int aim; } LAY[HP_NBTN] = {
    [HP_ATTACK]    = { "ATTACK",    M_BOTH, AN_R,    0,   0, 38, 1 },
    [HP_SPECIAL]   = { "SPECIAL",   M_BOTH, AN_R,  -76,  26, 24, 1 },
    [HP_CAST]      = { "CAST",      M_BOTH, AN_R,  -65, -46, 24, 1 },
    [HP_DASH]      = { "DASH",      M_BOTH, AN_R,    0, -80, 24, 0 },
    [HP_INTERACT]  = { "INTERACT",  M_BOTH, AN_R,   74,-108, 24, 0 },
    [HP_CALL]      = { "CALL",      M_BOTH, AN_R,   74, -41, 24, 0 },
    [HP_LOCK]      = { "LOCK",      M_BOTH, AN_R,   74,  26, 24, 0 },
    [HP_PAUSE]     = { "PAUSE",     M_BOTH, AN_T,  -84,  34, 17, 0 },
    [HP_INFO]      = { "INFO",      0, AN_T,  -68,  28, 17, 0 },
    [HP_MODE]      = { "MODE",      M_BOTH, AN_T, -124,  34, 17, 0 },   /* stick <-> D-pad */
    [HP_DPAD_UP]   = { "DPAD_UP",   M_MENU, AN_L,   56, -74, 24, 0 },
    [HP_DPAD_DOWN] = { "DPAD_DOWN", M_MENU, AN_L,   56,  26, 24, 0 },   /* same bottom gap as X; left gap below equals ZR to the right edge */
    [HP_DPAD_LEFT] = { "DPAD_LEFT", M_MENU, AN_L,    6, -24, 24, 0 },
    [HP_DPAD_RIGHT]= { "DPAD_RIGHT",M_MENU, AN_L,  106, -24, 24, 0 },
    [HP_CONFIRM]   = { "CONFIRM",   0, AN_R,   48,   0, 24, 0 },
    [HP_BACK]      = { "BACK",      0, AN_R,    0,  48, 24, 0 },
    [HP_ALT1]      = { "ALT1",      0, AN_R,    0, -48, 24, 0 },
    [HP_ALT2]      = { "ALT2",      0, AN_R,  -48,   0, 24, 0 },
    [HP_PAGE_PREV] = { "PAGE_PREV", M_BOTH, AN_R,   74,-175, 24, 0 },   /* Greet (L) */
    [HP_PAGE_NEXT] = { "PAGE_NEXT", 0, AN_C,   44,  28, 18, 0 },
};

static const struct { const char *en, *zh; } LABEL[HP_NBTN] = {
    [HP_ATTACK] = {"Attack","攻击"}, [HP_SPECIAL] = {"Special","特殊"}, [HP_CAST] = {"Cast","施法"},
    [HP_DASH] = {"Dash","冲刺"}, [HP_INTERACT] = {"Use","互动"}, [HP_CALL] = {"Auto","自动连击"},
    [HP_LOCK] = {"Hex","巫咒"}, [HP_PAUSE] = {"Pause","暂停"}, [HP_INFO] = {"Info","信息"},
    [HP_MODE] = {"Menu","菜单"},
    [HP_DPAD_UP] = {"\xE2\x96\xB2","\xE2\x96\xB2"}, [HP_DPAD_DOWN] = {"\xE2\x96\xBC","\xE2\x96\xBC"},
    [HP_DPAD_LEFT] = {"\xE2\x97\x80","\xE2\x97\x80"}, [HP_DPAD_RIGHT] = {"\xE2\x96\xB6","\xE2\x96\xB6"},
    [HP_CONFIRM] = {"OK","确认"}, [HP_BACK] = {"Back","返回"},
    [HP_ALT1] = {"Option 1","功能1"}, [HP_ALT2] = {"Option 2","功能2"},
    [HP_PAGE_PREV] = {"Greet","致意"}, [HP_PAGE_NEXT] = {"Next","下页"},
};

static const float STICK_R_DP = 56.f, STICK_DEAD = .10f, AIM_THR_DP = 10.f, AIM_FULL_DP = 50.f, HIT_SLOP = 1.3f;

const char *hp_label(hp_btn b, int lang) { return b >= 0 && b < HP_NBTN ? (lang ? LABEL[b].zh : LABEL[b].en) : ""; }

/* ---- config ---- */
static const struct { const char *name; uint32_t bit; } KEYS[] = {
    {"A",HP_NPAD_A},{"B",HP_NPAD_B},{"X",HP_NPAD_X},{"Y",HP_NPAD_Y},{"LS",HP_NPAD_LS},{"RS",HP_NPAD_RS},
    {"L",HP_NPAD_L},{"R",HP_NPAD_R},{"ZL",HP_NPAD_ZL},{"ZR",HP_NPAD_ZR},{"PLUS",HP_NPAD_PLUS},{"MINUS",HP_NPAD_MINUS},
    {"LEFT",HP_NPAD_LEFT},{"UP",HP_NPAD_UP},{"RIGHT",HP_NPAD_RIGHT},{"DOWN",HP_NPAD_DOWN},
};

void hp_config_default(hp_config *c) {
    memset(c, 0, sizeof *c);
    /* ATTACK/SPECIAL/CAST/DASH are the game's defaults as reported by the user (Y, X, A, B).
     * INTERACT, CALL and LOCK are still GUESSES: check the controls screen and override with hp_config_parse. */
    c->mask[HP_ATTACK] = HP_NPAD_Y;   c->mask[HP_SPECIAL] = HP_NPAD_X;  c->mask[HP_CAST] = HP_NPAD_A;
    c->mask[HP_DASH] = HP_NPAD_B;     c->mask[HP_INTERACT] = HP_NPAD_R;  c->mask[HP_CALL] = HP_NPAD_ZL;
    c->mask[HP_LOCK] = HP_NPAD_ZR;   /* Hex (巫咒) */
    c->mask[HP_PAUSE] = HP_NPAD_PLUS; c->mask[HP_INFO] = HP_NPAD_MINUS;
    c->mask[HP_DPAD_UP] = HP_NPAD_UP; c->mask[HP_DPAD_DOWN] = HP_NPAD_DOWN;
    c->mask[HP_DPAD_LEFT] = HP_NPAD_LEFT; c->mask[HP_DPAD_RIGHT] = HP_NPAD_RIGHT;
    c->mask[HP_CONFIRM] = HP_NPAD_A;  c->mask[HP_BACK] = HP_NPAD_B;
    c->mask[HP_ALT1] = HP_NPAD_X;     c->mask[HP_ALT2] = HP_NPAD_Y;
    c->mask[HP_PAGE_PREV] = HP_NPAD_L; c->mask[HP_PAGE_NEXT] = HP_NPAD_R;
    /* HP_MODE presses nothing: it only toggles the layout. */
}

static int ieq(const char *a, size_t n, const char *b) {
    if (strlen(b) != n) return 0;
    for (size_t i = 0; i < n; i++) if (toupper((unsigned char)a[i]) != b[i]) return 0;
    return 1;
}

int hp_config_parse(hp_config *c, const char *text) {
    int applied = 0, bad = 0, line = 0;
    const char *p = text;
    while (*p) {
        const char *e = p; while (*e && *e != '\n') e++;
        const char *q = p; line++;
        while (q < e && isspace((unsigned char)*q)) q++;
        const char *end = q; while (end < e && *end != '#') end++;
        while (end > q && isspace((unsigned char)end[-1])) end--;
        if (end > q) {
            const char *eq = q; while (eq < end && *eq != '=') eq++;
            int id = -1;
            if (eq < end) {
                const char *ne = eq; while (ne > q && isspace((unsigned char)ne[-1])) ne--;
                for (int i = 0; i < HP_NBTN; i++) if (ieq(q, (size_t)(ne - q), LAY[i].name)) id = i;
            }
            uint32_t mask = 0; int ok = id >= 0 && id != HP_MODE;
            if (ok) {
                const char *k = eq + 1;
                while (k < end && isspace((unsigned char)*k)) k++;
                if (k >= end) ok = 0;
                while (ok && k < end) {
                    const char *ke = k; while (ke < end && *ke != '+') ke++;
                    const char *kt = ke; while (kt > k && isspace((unsigned char)kt[-1])) kt--;
                    uint32_t bit = 0;
                    for (size_t i = 0; i < sizeof KEYS / sizeof *KEYS; i++) if (ieq(k, (size_t)(kt - k), KEYS[i].name)) bit = KEYS[i].bit;
                    if (!bit) ok = 0; else mask |= bit;
                    int sep = ke < end;
                    k = sep ? ke + 1 : ke;
                    while (k < end && isspace((unsigned char)*k)) k++;
                    if (sep && k >= end) ok = 0;   /* trailing '+' */
                }
            }
            if (ok) { c->mask[id] = mask; applied++; } else if (!bad) bad = line;
        }
        p = *e ? e + 1 : e;
    }
    return bad ? -bad : applied;
}

/* ---- geometry ---- */
static void center_of(const hp_state *s, int b, float *x, float *y) {
    float u = s->density, ax, ay;
    switch (LAY[b].anchor) {
    case AN_R: ax = s->w - s->inset_r - 160 * u;  ay = s->h - s->inset_b - 84 * u; break;
    case AN_L: ax = 80 * u;                      ay = s->h - s->inset_b - 84 * u; break;
    case AN_T: ax = s->w - s->inset_r;           ay = 0; break;
    default:   ax = s->w * .5f;                  ay = 0; break;
    }
    *x = ax + LAY[b].dx * u; *y = ay + LAY[b].dy * u;
}
static float radius_of(const hp_state *s, int b) { return LAY[b].r * s->density; }
static int visible(const hp_state *s, int b) { return (LAY[b].modes >> s->mode) & 1 ? 1 : 0; }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static int hit_button(const hp_state *s, float x, float y) {
    int best = -1; float bd = 1e30f;
    for (int b = 0; b < HP_NBTN; b++) {
        if (!visible(s, b)) continue;
        float cx, cy; center_of(s, b, &cx, &cy);
        float d = hypotf(x - cx, y - cy);
        if (d <= radius_of(s, b) * HIT_SLOP && d < bd) { bd = d; best = b; }
    }
    return best;
}

void hp_init(hp_state *s, const hp_config *c) {
    memset(s, 0, sizeof *s);
    if (c) s->cfg = *c; else hp_config_default(&s->cfg);
    s->density = 1.f;
}
void hp_resize(hp_state *s, float w, float h, float density, float il, float ir, float ib) {
    s->w = w; s->h = h; s->density = density > 0 ? density : 1.f; s->inset_l = il; s->inset_r = ir; s->inset_b = ib;
    s->np = 0;   /* positions from the old geometry are meaningless */
}
void hp_set_mode(hp_state *s, hp_mode m) { s->mode = m; s->np = 0; }

void hp_touch(hp_state *s, const int *ids, const float *xs, const float *ys, int n, int cancelled) {
    if (cancelled || n <= 0) { s->np = 0; return; }
    /* drop pointers that went up */
    int k = 0;
    for (int i = 0; i < s->np; i++) {
        int still = 0; for (int j = 0; j < n; j++) if (ids[j] == s->p[i].id) still = 1;
        if (still) s->p[k++] = s->p[i];
    }
    s->np = k;
    for (int j = 0; j < n; j++) {
        int idx = -1;
        for (int i = 0; i < s->np; i++) if (s->p[i].id == ids[j]) idx = i;
        if (idx >= 0) { s->p[idx].x = xs[j]; s->p[idx].y = ys[j]; continue; }
        if (s->np >= HP_MAX_POINTERS) continue;
        int role = ROLE_NONE, btn = -1;
        float x = xs[j], y = ys[j];
        int hb = hit_button(s, x, y);
        if (hb == HP_MODE) {
            s->mode = s->mode == HP_MODE_PLAY ? HP_MODE_MENU : HP_MODE_PLAY;
            for (int i = 0; i < s->np; i++) s->p[i].role = ROLE_NONE;   /* nothing may stay pressed across the switch */
        } else if (hb >= 0) { role = ROLE_BTN; btn = hb; }
        else if (s->mode == HP_MODE_PLAY && x < s->w * .5f && y > s->h * .18f) {
            int have = 0; for (int i = 0; i < s->np; i++) if (s->p[i].role == ROLE_STICK) have = 1;
            if (!have) { s->sbx = x; s->sby = y; role = ROLE_STICK; }   /* base = touch-down point, so no jump at contact */
        }
        int i = s->np++;
        s->p[i].id = ids[j]; s->p[i].role = role; s->p[i].btn = btn;
        s->p[i].ox = s->p[i].x = x; s->p[i].oy = s->p[i].y = y;
    }
    /* the floating base follows a finger dragged beyond the stick radius */
    for (int i = 0; i < s->np; i++) if (s->p[i].role == ROLE_STICK) {
        float r = STICK_R_DP * s->density, dx = s->p[i].x - s->sbx, dy = s->p[i].y - s->sby, d = hypotf(dx, dy);
        if (d > r) { s->sbx = s->p[i].x - dx / d * r; s->sby = s->p[i].y - dy / d * r; }
    }
}

static int first_aim(const hp_state *s) {
    for (int i = 0; i < s->np; i++) if (s->p[i].role == ROLE_BTN && LAY[s->p[i].btn].aim) return i;
    return -1;
}

void hp_output(const hp_state *s, hp_out *o) {
    memset(o, 0, sizeof *o);
    for (int i = 0; i < s->np; i++) {
        if (s->p[i].role == ROLE_BTN) o->buttons |= s->cfg.mask[s->p[i].btn];
        else if (s->p[i].role == ROLE_STICK) {
            float r = STICK_R_DP * s->density, dx = s->p[i].x - s->sbx, dy = s->p[i].y - s->sby, d = hypotf(dx, dy);
            float m = clampf(d / r, 0, 1);
            if (m > STICK_DEAD && d > 0) {
                m = (m - STICK_DEAD) / (1 - STICK_DEAD);
                o->lx = (int32_t)lroundf(dx / d * m * 32767.f); o->ly = (int32_t)lroundf(-dy / d * m * 32767.f);
            }
        }
    }
    int a = first_aim(s);
    if (a >= 0) {
        float dx = s->p[a].x - s->p[a].ox, dy = s->p[a].y - s->p[a].oy, d = hypotf(dx, dy);
        float thr = AIM_THR_DP * s->density, full = AIM_FULL_DP * s->density;
        if (d > thr) {
            float m = clampf((d - thr) / (full - thr), 0, 1);
            o->rx = (int32_t)lroundf(dx / d * m * 32767.f); o->ry = (int32_t)lroundf(-dy / d * m * 32767.f);
        }
    }
}

void hp_visual(const hp_state *s, hp_vis *v) {
    memset(v, 0, sizeof *v);
    for (int b = 0; b < HP_NBTN; b++) {
        if (!visible(s, b)) continue;
        hp_vis_btn *e = &v->btns[v->nbtns++];
        e->btn = b; center_of(s, b, &e->cx, &e->cy); e->r = radius_of(s, b);
        for (int i = 0; i < s->np; i++) if (s->p[i].role == ROLE_BTN && s->p[i].btn == b) e->pressed = 1;
    }
    for (int i = 0; i < s->np; i++) if (s->p[i].role == ROLE_STICK) {
        float r = STICK_R_DP * s->density, dx = s->p[i].x - s->sbx, dy = s->p[i].y - s->sby, d = hypotf(dx, dy);
        /* only the drawn base is pulled inside the screen; the logical base (and so the output) is untouched */
        float m = r + 8 * s->density;
        v->stick_active = 1; v->stick_r = r;
        v->base_x = clampf(s->sbx, s->inset_l + m, s->w * .5f); v->base_y = clampf(s->sby, s->h * .18f + m, s->h - s->inset_b - m);
        float k = d > r ? r / d : 1.f; v->knob_x = v->base_x + dx * k; v->knob_y = v->base_y + dy * k;
    }
    int a = first_aim(s);
    if (a >= 0) { v->aim_active = 1; v->aim_x0 = s->p[a].ox; v->aim_y0 = s->p[a].oy; v->aim_x1 = s->p[a].x; v->aim_y1 = s->p[a].y; }
}
