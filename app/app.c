/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 hacha
 * Started from fm1-base's template app (charlesvestal/fm1-base, template/app.c, MPL-2.0, by Charles Vestal) and
 * rewritten; a few of its idioms remain (the settings' defaults, app_say(), the keys' edges). The pages, the rule
 * over the knobs and the knob strip follow PaperLagoon's (after HYP-1's). */
/* RhinoTiger on fm1-base: the app's unity build (compiled into fm1-base's, after the platform: gfx.c and
 * announce.c are in scope), the glue to the engine (dsp/psy.c, its own -O2 unit) and the screen.
 *
 * Pages, as HYP-1's: SELECT turns through all of them, a button jumps to its own (again: the next of its own) and is
 * lit while one of its pages shows; the rule over the knobs says which page, and where it is among them.
 *   1 MAIN (HOME)            the macros: K1..K4 Energy, Detail, Dark, Drift
 *   2 GROOVE (HOME again)    K1..K4 Tempo (the world's, moved), Root, Fills (how often), Sit out (how often a part
 *                            rests a phrase)
 *   3 KICK (SEQ)             K1..K4 Level, Tune, Length, Punch
 *   4 PERC (SEQ again)       K1..K4 Level, Hats, Tone, Clap
 *   5 BASS (LFO)             K1..K4 Level, Bite, Length, Drive
 *   6 EVENTS (ARP)           K1..K4 Level, Density, Runs, Echo (the send)
 *   7 ECHO (FX)              K1..K4 Level, Feedback, Tone, Time
 *   8 GLOBAL (GLO)           K1..K2 Lights (the buttons' glow), Theme; SAVE here keeps it all
 * SEL held: the knobs are MAIN's four on any page. PRESETS: the world, from the next bar. PLAY: play or rest.
 * The keys play no notes: each throws an event into the music on the next sixteenth (dsp/events.c); the screen
 * names it at once. */
#include "fastmath.h"
#include "psy.h"

#define SET_MAGIC 0x31544852u            /* "RHT1": the settings record */
typedef struct {
    uint32_t magic;
    uint8_t world;
    uint8_t lights;                      /* the buttons' glow: 0 off, 1 low, 2 high */
    uint8_t theme;                       /* gfx.c PALETTES */
    uint8_t rsv;
    int16_t par[P_NPARAMS];
} settings_t;
static settings_t set;
static uint8_t set_dirty;                /* changed since the last save: SAVE is lit */


/* ------------------------------------------------------------------ audio --- */
static float eng_l[APP_BLOCK], eng_r[APP_BLOCK];
static float gain_now;                   /* glides to MASTER: no step at a block's edge */

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    float gain = (float)master_q12 / 4096.0f * 8300000.0f;   /* MASTER, read once a block */
    ps_render(eng_l, eng_r, n);
    for (i = 0; i < n; i++) {
        gain_now += (gain - gain_now) * 0.004f;                  /* ~5 ms: turning MASTER does not click */
        lr[2 * i] = (int32_t)(2.0f * eng_l[i] * gain_now);
        lr[2 * i + 1] = (int32_t)(2.0f * eng_r[i] * gain_now);
    }
}

void app_panic(void) { ps_play(0); }

/* --------------------------------------------------------------- settings --- */
static void settings_defaults(void)
{
    int p;
    set.magic = SET_MAGIC;
    set.world = 0;
    set.lights = 1;
    set.theme = APP_PALETTE;
    for (p = 0; p < P_NPARAMS; p++)
        set.par[p] = ps_param(p)->def;
}

static void settings_apply(void)
{
    int p;
    for (p = 0; p < P_NPARAMS; p++)
        ps_set(p, set.par[p]);
    ps_world(set.world);
    palette_set(set.theme);
}

void app_init(void)
{
    int p, ok;
    ps_init();
    ok = plat_store_load(OBJ_SETTINGS, &set, sizeof set) == (int)sizeof set && set.magic == SET_MAGIC &&
         set.world < PS_NWORLD && set.lights <= 2u && set.theme < NPALETTES;
    for (p = 0; ok && p < P_NPARAMS; p++)
        ok = set.par[p] >= ps_param(p)->lo && set.par[p] <= ps_param(p)->hi;
    if (!ok)
        settings_defaults();
    settings_apply();
}

/* ------------------------------------------------------------------ pages --- */
enum { PG_MAIN, PG_GROOVE, PG_KICK, PG_PERC, PG_BASS, PG_EVENTS, PG_ECHO, PG_GLOBAL, NPAGE };
static const char *const PAGE_NAME[NPAGE] = {"MAIN", "GROOVE", "KICK", "PERC", "BASS", "EVENTS", "ECHO", "GLOBAL"};
static const uint8_t PAGE_BTN[NPAGE] = {B_HOME, B_HOME, B_SEQ, B_SEQ, B_LFO, B_ARP, B_FX, B_GLO};
/* a page's first parameter (P_*), or -1 */
static int page_par(int p)
{
    static const int8_t FIRST[NPAGE] = {P_ENERGY, P_TEMPO, P_KICK_LEVEL, P_PERC_LEVEL, P_BASS_LEVEL, P_EV_LEVEL,
                                        P_ECHO_LEVEL, -1};
    return FIRST[clamp(p, 0, NPAGE - 1)];
}
enum { LANE_KICK, LANE_BASS, LANE_PERC, LANE_EVENTS, NLANE };
static const char *const LANE_NAME[NLANE] = {"KICK", "BASS", "PERC", "EVENTS"};
static int page_lane(int p)              /* the lane a page acts on (the screen brightens it), or -1 */
{
    return p == PG_KICK ? LANE_KICK : p == PG_BASS ? LANE_BASS : p == PG_PERC ? LANE_PERC : p == PG_EVENTS ? LANE_EVENTS : -1;
}

static struct {
    char msg[2][32];
    uint32_t msg_until;
    uint32_t keys, btn;
    uint32_t turned_at[4];               /* each knob's last turn (the strip lights it) */
    uint32_t last_draw;
    uint32_t seen[NHIT];                 /* the engine's counts as last drawn */
    int32_t mark[NLANE][16];             /* the bar each sixteenth was last struck in, per lane (-9: never) */
    uint32_t thrown_at;                  /* a key's event named on the screen since (0: none) */
    int8_t thrown_type;
    uint8_t page;
    uint8_t redraw;
    uint8_t shift;                       /* SEL held: the knobs are MAIN's */
} ui;

/* s copied into d (cap bytes with the terminator); returns the length written */
static uint32_t str_put(char *d, uint32_t cap, const char *s)
{
    uint32_t k = 0;
    if (!cap)
        return 0;
    for (; s && s[k] && k + 1 < cap; k++)
        d[k] = s[k];
    d[k] = 0;
    return k;
}

/* a whole number in decimal, a minus sign when below zero (b: 12 bytes). 32-bit only */
static void int_text(int32_t v, char *b)
{
    char t[10];
    uint32_t x = v < 0 ? 0u - (uint32_t)v : (uint32_t)v;
    int n = 0;
    if (v < 0)
        *b++ = '-';
    do {
        t[n++] = (char)('0' + x % 10u);
        x /= 10u;
    } while (x);
    while (n)
        *b++ = t[--n];
    *b = 0;
}

static void note_text(int note, char *b)  /* "F#" */
{
    static const char *const N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    str_put(b, 4, N[((note % 12) + 12) % 12]);
}

void app_say(const char *a, const char *b)
{
    char line[50];
    uint32_t k;
    str_put(ui.msg[0], sizeof ui.msg[0], a);
    str_put(ui.msg[1], sizeof ui.msg[1], b);
    ui.msg_until = plat_ms() + 1300u;
    ui.redraw = 1;
    if (!ann_on())
        return;
    k = str_put(line, sizeof line, ui.msg[0]);
    if (ui.msg[1][0] && k + 1 < sizeof line) {
        line[k++] = ' ';
        str_put(line + k, sizeof line - k, ui.msg[1]);
    }
    ann_say(line, 0, ANN_NOW);
}

/* a page: the rule shows it (its name, and where it is among the pages); a screen reader hears "name n of m" */
static void page_go(int p)
{
    char t[32];
    uint32_t k;
    p = clamp(p, 0, NPAGE - 1);
    if (p == ui.page)
        return;
    ui.page = (uint8_t)p;
    ui.redraw = 1;
    if (!ann_on())
        return;
    k = str_put(t, sizeof t, PAGE_NAME[p]);
    k += str_put(t + k, sizeof t - k, " ");
    int_text(p + 1, t + k);
    k += 1;
    k += str_put(t + k, sizeof t - k, " of ");
    int_text(NPAGE, t + k);
    ann_say(t, 0, ANN_NOW);
}

/* the knobs of a page, or (SEL held) MAIN's: K1..K4's names, values and positions */
static const char *knob_name(int page, int k)
{
    if (page == PG_GLOBAL)
        return k == 0 ? "LIGHTS" : k == 1 ? "THEME" : "";
    return ps_param(page_par(page) + k)->name;
}

static void par_text(int p, int v, char *t)   /* a parameter's value as the screen says it */
{
    static const char *const FILLS[5] = {"OFF", "32", "16", "8", "4"};
    if (p == P_TEMPO)
        int_text(ps_bpm(), t);
    else if (p == P_ROOT)
        note_text(ps_root(), t);
    else if (p == P_FILLS)
        str_put(t, 12, FILLS[clamp(v, 0, 4)]);
    else if (p == P_ECHO_TIME)
        str_put(t, 12, ps_echo_time_name(v));
    else if (p == P_KICK_TUNE && v > 0) {
        t[0] = '+';
        int_text(v, t + 1);
    } else
        int_text(v, t);
}

static void knob_value(int page, int k, char *t, int32_t *pos, int32_t *span)   /* text, and where in its range */
{
    if (page == PG_GLOBAL) {
        static const char *const LIGHTS[3] = {"OFF", "LOW", "HIGH"};
        *t = 0;
        *pos = 0;
        *span = 0;
        if (k == 0) {
            str_put(t, 12, LIGHTS[set.lights]);
            *pos = set.lights;
            *span = 2;
        } else if (k == 1) {
            str_put(t, 12, PALETTES[set.theme].name);
            *pos = set.theme;
            *span = (int32_t)NPALETTES - 1;
        }
        return;
    }
    {
        int p = page_par(page) + k;
        const ps_param_t *d = ps_param(p);
        par_text(p, set.par[p], t);
        *pos = set.par[p] - d->lo;
        *span = d->hi - d->lo;
    }
}

static void knob_turn(int k, int32_t d)
{
    int page = ui.shift ? PG_MAIN : ui.page;
    char t[16];
    int32_t pos, span;
    ui.turned_at[k] = plat_ms();
    ui.redraw = 1;
    if (page == PG_GLOBAL) {
        if (k == 0)
            set.lights = (uint8_t)clamp(set.lights + d, 0, 2);
        else if (k == 1) {
            set.theme = (uint8_t)clamp(set.theme + d, 0, (int32_t)NPALETTES - 1);
            palette_set(set.theme);
        } else
            return;
    } else {
        int p = page_par(page) + k;
        const ps_param_t *pi = ps_param(p);
        int step = pi->hi - pi->lo >= 100 ? 2 : 1;   /* 0..100 two a detent, the rest (tempo, root, ...) one */
        int32_t v = clamp(set.par[p] + step * d, pi->lo, pi->hi);
        if (v == set.par[p])
            return;
        set.par[p] = (int16_t)v;
        ps_set(p, (int)v);
    }
    set_dirty = 1;
    knob_value(page, k, t, &pos, &span);
    app_say(knob_name(page, k), t);
}

static void world_step(int32_t d)
{
    set.world = (uint8_t)(((int32_t)set.world + d % PS_NWORLD + PS_NWORLD) % PS_NWORLD);
    set_dirty = 1;
    ps_world(set.world);
    app_say("WORLD", ps_world_name(set.world));
}

static void save(void)
{
    if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0) {
        set_dirty = 0;
        app_say("SAVED", "");
    } else {
        app_say("SAVE", "FAILED");
    }
}

/* ------------------------------------------------------------------ input --- */
static void input(void)
{
    uint32_t keys, btn = plat_buttons(), k, down, up;
    int32_t e;
    touch_poll();                            /* the sensed keys' presses, velocities and pressure */
    keys = touch_keys();
    down = btn & ~ui.btn;
    up = ui.btn & ~btn;
    for (k = 0; k < NKEYS; k++) {            /* the keys throw events (key 0 is F3) */
        uint32_t bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit))
            ps_key((int)k, 1, touch_vel(k));
        else if (!(keys & bit) && (ui.keys & bit))
            ps_key((int)k, 0, 0);
        else if (keys & bit)
            ps_key((int)k, 2, touch_pressure(k));
    }
    if (down & 1u << B_PLAY) {
        ps_play(!ps_playing());
        app_say(ps_playing() ? "RESTING" : "PLAYING", "");   /* (it changes as the next block starts) */
    }
    if (down & 1u << B_SAVE) {
        if (ui.page == PG_GLOBAL)
            save();
        else
            app_say("SAVE", "ON GLOBAL");
    }
    if (down & 1u << B_HOME)
        page_go(ui.page == PG_MAIN ? PG_GROOVE : PG_MAIN);
    if (down & 1u << B_SEQ)
        page_go(ui.page == PG_KICK ? PG_PERC : PG_KICK);
    if (down & 1u << B_LFO)
        page_go(PG_BASS);
    if (down & 1u << B_ARP)
        page_go(PG_EVENTS);
    if (down & 1u << B_FX)
        page_go(PG_ECHO);
    if (down & 1u << B_GLO)
        page_go(PG_GLOBAL);
    ui.shift = (uint8_t)((btn >> B_SEL) & 1u);
    if ((down | up) & 1u << B_SEL)
        ui.redraw = 1;
    if ((e = plat_enc(EN_SELECT)) != 0)
        page_go((int)ui.page + (int)e);
    if ((e = plat_enc(EN_PRESET)) != 0)
        world_step(e);
    (void)plat_enc(EN_ALGO);                 /* (not yet: the voices' sounds) */
    for (k = 0; k < 4; k++)
        if ((e = plat_enc((int)(EN_K1 + k))) != 0)
            knob_turn((int)k, e);
    if (keys != ui.keys || btn != ui.btn)
        ui.redraw = 1;
    ui.keys = keys;
    ui.btn = btn;
    {   /* lit: the page's button, PLAY playing, SAVE unsaved; dim: the buttons that have a page or a use */
        uint32_t lit = btn | 1u << PAGE_BTN[ui.page] | (set_dirty ? 1u << B_SAVE : 0u) | (ps_playing() ? 1u << B_PLAY : 0u);
        uint32_t dim = 1u << B_HOME | 1u << B_SEQ | 1u << B_LFO | 1u << B_ARP | 1u << B_FX | 1u << B_GLO | 1u << B_SAVE |
                       1u << B_PLAY | 1u << B_SEL;
        plat_lights(lit, keys, dim, 0, set.lights);
    }
}

void app_input_only(void) { input(); }

/* ----------------------------------------------------------------- screen --- */
static void draw_header(uint32_t now)
{
    char t[12];
    int32_t x;
    cv_begin(240, 32, C_BLACK);
    if (now < ui.msg_until) {                /* a message over the header */
        int big = 18 + text_w(&FONT_B, ui.msg[0]) + text_w(&FONT_B, ui.msg[1]) <= 230;
        int32_t y = big ? 6 : 9;
        x = cv_text(10, y, big ? &FONT_B : &FONT_XS, ui.msg[0], C_WHITE);
        cv_text(x + 8, y, big ? &FONT_B : &FONT_XS, ui.msg[1], C_HI);
    } else {
        uint32_t tick = ps_ui.tick, beat = tick / PS_TPB % 4u;
        cv_text(10, 6, &FONT_B, ps_world_name(set.world), ps_playing() ? C_WHITE : C_GRAY);
        int_text(ps_bpm(), t);
        x = 230 - text_w(&FONT_B, t) - 14;
        cv_text(x, 6, &FONT_B, t, C_HI);
        if (ps_playing())                    /* the beat: a lamp, bright on the beat's first sixteenth */
            cv_rect(222, 12, 8, 8, tick % PS_TPB < 6u ? (beat == 0u ? C_WHITE : C_HI) : C_LINE);
    }
    cv_blit(0, 0);
}

/* the lanes: what struck, on the bar's sixteen sixteenths; the page's lane brighter; under them the phrase's eight
 * bars, and a thrown event's name */
static void marks_update(void)
{
    static const uint8_t LANE_OF[NHIT] = {LANE_KICK, LANE_BASS, LANE_PERC, LANE_EVENTS, LANE_EVENTS};
    uint32_t tick = ps_ui.tick ? ps_ui.tick - 1u : 0u, h, thrown = ps_ui.hits[HIT_THROW];
    int32_t bar = (int32_t)(tick / (4u * PS_TPB)), six = (int32_t)(tick / 6u % 16u);
    if (thrown != ui.seen[HIT_THROW]) {      /* a key's event: named at once */
        ui.thrown_type = ps_ui.thrown_type;
        ui.thrown_at = plat_ms();
    }
    for (h = 0; h < NHIT; h++) {
        uint32_t c = ps_ui.hits[h];
        if (c != ui.seen[h]) {
            ui.seen[h] = c;
            ui.mark[LANE_OF[h]][six] = bar;
        }
    }
}

static void draw_lanes(uint32_t now)
{
    uint32_t tick = ps_ui.tick ? ps_ui.tick - 1u : 0u;
    int32_t bar = (int32_t)(tick / (4u * PS_TPB)), six = (int32_t)(tick / 6u % 16u), lane, i;
    int focus = page_lane(ui.page);
    cv_begin(240, 148, C_BLACK);
    for (lane = 0; lane < NLANE; lane++) {
        int32_t y = 8 + lane * 26;
        int f = lane == focus;
        cv_text(8, y + 3, &FONT_XS, LANE_NAME[lane], f ? C_HI : C_GRAY);
        for (i = 0; i < 16; i++) {
            int32_t x = 66 + i * 10, mk = ui.mark[lane][i];
            uint16_t c = mk == bar ? (f ? C_WHITE : C_HI) : mk == bar - 1 ? (f ? C_AMB : C_DIM) : C_LINE;
            cv_rect(x, y + (i % 4 ? 4 : 2), 7, i % 4 ? 10 : 14, c);
            if (ps_playing() && i == six)
                cv_rect(x, y + 18, 7, 2, C_WHITE);
        }
    }
    {   /* the phrase: eight bars */
        int32_t inph = bar % 8;
        cv_text(8, 112, &FONT_XS, "PHRASE", C_GRAY);
        for (i = 0; i < 8; i++)
            cv_rect(66 + i * 20, 116, 16, 4, i == inph && ps_playing() ? C_HI : i < inph ? C_DIM : C_LINE);
    }
    if (ui.thrown_at && now - ui.thrown_at < 900u && ui.thrown_type >= 0) {   /* a key's event, named */
        const char *n = ps_event_name(ui.thrown_type);
        int32_t w = text_w(&FONT_B, n);
        cv_rect(236 - w - 12, 126, w + 8, 20, C_LINE);
        cv_text(236 - w - 8, 128, &FONT_B, n, C_WHITE);
    }
    cv_blit(0, 32);
}

/* the rule over the knobs (after HYP-1's): at its left what the knobs act on if it needs saying (SEL held: MAIN's
 * four; GLOBAL with something unsaved: SAVE keeps it); at its right the page's name, then a short bar a page, the one
 * shown lit, its button's other page a little brighter */
static void draw_rule(void)
{
    const char *ctx = 0, *name = ui.shift ? "MAIN" : PAGE_NAME[ui.page];
    int32_t xd = 231 - NPAGE * 5, xt = xd - 6 - text_w(&FONT_XS, name), xs = 0, i;
    if (ui.shift)
        ctx = "SEL: MAIN";
    else if (ui.page == PG_GLOBAL && set_dirty)
        ctx = "SAVE: KEEP IT";
    if (ctx)
        xs = cv_text(6, 0, &FONT_XS, ctx, C_AMB) + 6;
    if (xt - 6 > xs)
        cv_rect(xs, 5, xt - 6 - xs, 1, C_LINE);
    cv_text(xt, 0, &FONT_XS, name, C_HI);
    for (i = 0; i < NPAGE; i++) {
        int cur = i == (int32_t)ui.page, group = PAGE_BTN[i] == PAGE_BTN[ui.page];
        cv_rect(xd + i * 5, cur ? 2 : 3, 3, cur ? 7 : 5, cur ? C_HI : group ? C_GRAY : C_LINE);
    }
}

static void draw_knobs(uint32_t now)
{
    char t[16];
    int32_t i;
    int page = ui.shift ? PG_MAIN : ui.page;
    cv_begin(240, 60, C_BLACK);
    draw_rule();
    for (i = 0; i < 4; i++) {
        int32_t x = 6 + i * 59, w = 52, pos, span;
        int lit = ui.turned_at[i] && now - ui.turned_at[i] < 600u;
        const char *name = knob_name(page, (int)i);
        if (!name[0])
            continue;
        knob_value(page, (int)i, t, &pos, &span);
        cv_text(x, 14, &FONT_XS, name, lit ? C_WHITE : C_GRAY);
        cv_text(x, 29, &FONT_B, t, lit ? C_WHITE : C_HI);
        cv_rect(x, 51, w, 2, C_LINE);
        if (span > 0)
            cv_rect(x, 51, w * pos / span, 2, lit ? C_WHITE : C_HI);
    }
    cv_blit(0, 180);
}

static void draw(void)
{
    uint32_t now = plat_ms();
    draw_header(now);
    draw_lanes(now);
    draw_knobs(now);
}

void app_ui_init(void)
{
    int l, i;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    for (l = 0; l < NLANE; l++)
        for (i = 0; i < 16; i++)
            ui.mark[l][i] = -9;
    ui.thrown_type = -1;
    ui.redraw = 1;
}

void app_frame(void)
{
    uint32_t now = plat_ms();
    input();
    ann_tick(now);
    marks_update();
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (ui.redraw || now - ui.last_draw >= 33u) {   /* the lanes move: about 30 frames a second */
        ui.redraw = 0;
        ui.last_draw = now;
        draw();
    }
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "world"))
        *got = set.world;
    else if (!strcmp(what, "page"))          /* 0 MAIN .. 7 GLOBAL */
        *got = ui.page;
    else if (!strcmp(what, "bar"))           /* bars since PLAY */
        *got = ps_bar();
    else if (!strcmp(what, "playing"))
        *got = ps_playing();
    else if (!strcmp(what, "dirty"))
        *got = set_dirty;
    else if (!strcmp(what, "lights"))
        *got = set.lights;
    else if (!strcmp(what, "bpm"))
        *got = ps_bpm();
    else if (!strcmp(what, "events"))        /* events begun */
        *got = ps_stat(0);
    else if (!strcmp(what, "thrown"))        /* thrown by the keys and remembered (up to 8) */
        *got = ps_stat(1);
    else if (!strcmp(what, "thrown_type"))
        *got = ps_stat(2);
    else if (!strncmp(what, "param", 5))     /* paramN: parameter N (psy.h P_*) as the app keeps it */
        *got = set.par[atoi(what + 5) % P_NPARAMS];
    else if (!strncmp(what, "engine", 6))    /* engineN: as the engine has it */
        *got = ps_get(atoi(what + 6));
    else
        return 0;
    return 1;
}
#endif
