/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 hacha
 * Started from fm1-base's template app (charlesvestal/fm1-base, template/app.c, MPL-2.0, by Charles Vestal) and
 * rewritten; a few of its idioms remain (the settings' defaults, app_say(), the keys' edges). The pages, the rule
 * over the knobs and the knob strip follow PaperLagoon's (after HYP-1's). */
/* RhinoTiger on fm1-base: the app's unity build (compiled into fm1-base's, after the platform: gfx.c and
 * announce.c are in scope), the glue to the engine (dsp/psy.c, its own -O2 unit) and the screen.
 *
 * Pages, as HYP-1's: SELECT turns through all of them, a button jumps to its own (again: the next of its own) and is
 * lit while one of its pages shows; the rule over the knobs says which page, and where it is among them. HOME holds
 * the whole; the top row's middle four (SEL, ENV, LFO, EDIT) the sounds, whatever their names; SEQ the pattern of the
 * part shown (the line's for now: again, back to the part's sound; held, the pattern drawn anew from the next bar).
 *   1 MAIN (HOME)            the macros: K1..K4 Energy, Detail, Dark, Drift
 *   2 GROOVE (HOME again)    K1..K4 Tempo (the world's, moved), Root, Fills (how often), Sit out (how often a part
 *                            rests a phrase)
 *   3 PHASE (HOME again)     K1..K4 Breaks (how often one comes on its own; 0: only when cued), Break (its bars: the
 *                            world's, 4..32), Build (2, 4, 8 bars), Gap (bars after a drop before one comes on its own)
 *   4 KICK (SEL)             K1..K4 Level, Tune, Drive, Punch
 *   5 BASS (SEL again)       K1..K4 Level, Bite, Length, Drive
 *   6 PERC (ENV)             K1..K4 Level, Hats, Tone, Clap
 *   7 LINE (LFO)             K1..K4 Level, Bite (the modulation's height), Decay (its fall), Length
 *   8 LINE SEQ (SEQ on LINE) K1..K4 Density, Mutate, Range (how far the notes go up), Slide (how many)
 *   9 EVENTS (EDIT)          K1..K4 Level, Density, Runs, Echo (the send)
 *  10 ATMOS (EDIT again)    K1..K4 Level, Colour, Air, Wash (the send)
 *  11 ECHO (FX)              K1..K4 Level, Feedback, Tone, Time
 *  12 GLOBAL (GLO)           K1..K2 Lights (the buttons' glow), Theme; K3 shows the audio's load (CPU); K4 MIDI in
 *                            (off by default); SAVE here keeps it all
 * PRESETS: the world, from the next bar. PLAY: play or rest. ALGORITHM: the type of the page's part (elsewhere the
 * bass's colour; a drop chooses another on its own). OCT- / OCT+: the keys' events an octave down or up (-1..+2; both
 * lit away from 0). REC held: the keys' throws taken into a loop of two bars (pressed again: a take anew; tapped: the
 * loop gone; lit while one plays). ARP: nothing yet.
 * SAVE (but on GLOBAL), after HYP-1's: tapped, the next section from the next bar (the groove: a break; a break: its
 * build, then the drop); held, the drop at the next bar. Lit in a break, quick in a build.
 * The keys play no notes: each throws an event into the music on the next sixteenth (dsp/events.c); the screen
 * names it at once. On ECHO (FX) they are the performance instead (dsp/fx.c), and the screen
 * shows them: the white keys a grid of sixteen effects, four rows of four (REPEAT, FILTER, SPACE, MOTION; each row's
 * first key glows), each while held; the black keys the parts, F#3 G#3 A#3 C#4 D#4 F#4 muted (tapped, again: back:
 * KICK, BASS, PERC, LINE, EVENTS, ATMOS), G#4 a fill on the next beat, A#4 and C#5 the kick and the bass out while
 * held, F#5 every part back. A key does what it did when pressed until let go. */
#include "fastmath.h"
#include "psy.h"
#include "logo.h"

#define SET_MAGIC 0x34544852u            /* "RHT4": the settings record (2: the atmos's four; 3: the line's, 8 worlds;
                                          * 4: PHASE's four, the breaks' switch gone into BREAKS) */
typedef struct {
    uint32_t magic;
    uint8_t world;
    uint8_t lights;                      /* the buttons' glow: 0 off, 1 low, 2 high */
    uint8_t theme;                       /* gfx.c PALETTES */
    uint8_t flags;                       /* SET_MIDI */
    int16_t par[P_NPARAMS];
} settings_t;
static settings_t set;
#define SET_MIDI 2u                      /* MIDI in: notes throw events, start / stop / clock followed */
static void midi_off(void);
static uint8_t set_dirty;                /* changed since the last save: SAVE is lit */


/* ------------------------------------------------------------------ audio --- */
static float eng_l[APP_BLOCK], eng_r[APP_BLOCK];
static float gain_now;                   /* glides to MASTER: no step at a block's edge */

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    float gain = (float)master_q12 / 4096.0f * 8388000.0f;   /* MASTER, read once a block: the engine's 1.0 (its
                                                                * limit) is full scale at the top */
    ps_render(eng_l, eng_r, n);
    for (i = 0; i < n; i++) {
        gain_now += (gain - gain_now) * 0.004f;                  /* ~5 ms: turning MASTER does not click */
        lr[2 * i] = (int32_t)(eng_l[i] * gain_now);
        lr[2 * i + 1] = (int32_t)(eng_r[i] * gain_now);
    }
}

void app_panic(void) { ps_play(0); }

static volatile uint32_t shed_at;        /* the render last over its time (plat_ms; 0: not lately) */
void app_overload(void)                  /* the render over 90 % of its time: lighter, for 8 seconds after the last */
{
    ps_shed(1);
    shed_at = plat_ms() | 1u;
}

/* --------------------------------------------------------------- settings --- */
static void settings_defaults(void)
{
    int p;
    set.magic = SET_MAGIC;
    set.world = 2;                       /* COLD SIGNAL */
    set.lights = 1;
    set.flags = 0;
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
         set.world < PS_NWORLD && set.lights <= 2u && set.theme < NPALETTES &&
         (set.flags & ~SET_MIDI) == 0u;
    for (p = 0; ok && p < P_NPARAMS; p++)
        ok = set.par[p] >= ps_param(p)->lo && set.par[p] <= ps_param(p)->hi;
    if (!ok)
        settings_defaults();
    settings_apply();
}

/* ------------------------------------------------------------------ pages --- */
/* the pages in the panel's order: HOME the whole (MAIN, GROOVE, PHASE); the top row's middle four the sounds (SEL the
 * kick and the bass, ENV the percussion, LFO the line, EDIT the events and the atmos); FX the echo; GLO the global.
 * A button with two or three pages steps through them */
enum { PG_MAIN, PG_GROOVE, PG_PHASE, PG_KICK, PG_BASS, PG_PERC, PG_LINE, PG_LINE_SEQ, PG_EVENTS, PG_ATMOS, PG_ECHO,
       PG_GLOBAL, NPAGE };
static const char *const PAGE_NAME[NPAGE] = {"MAIN", "GROOVE", "PHASE", "KICK", "BASS", "PERC", "LINE", "LINE SEQ",
                                             "EVENTS", "ATMOS", "ECHO", "GLOBAL"};
static const uint8_t PAGE_BTN[NPAGE] = {B_HOME, B_HOME, B_HOME, B_SEL, B_SEL, B_ENV, B_LFO, B_SEQ, B_EDIT, B_EDIT, B_FX,
                                        B_GLO};
/* a page's knobs: K1..K4's parameters (P_*; -1 none) */
static int page_par(int p, int k)
{
    static const int8_t PAR[NPAGE][4] = {
        {P_ENERGY, P_DETAIL, P_DARK, P_DRIFT},
        {P_TEMPO, P_ROOT, P_FILLS, P_SITOUT},
        {P_BREAKS, P_BREAK_LEN, P_BUILD_LEN, P_GAP},
        {P_KICK_LEVEL, P_KICK_TUNE, P_KICK_DRIVE, P_KICK_PUNCH},
        {P_BASS_LEVEL, P_BASS_BITE, P_BASS_LEN, P_BASS_DRIVE},
        {P_PERC_LEVEL, P_HATS, P_HAT_TONE, P_CLAP},
        {P_LINE_LEVEL, P_LINE_BITE, P_LINE_DECAY, P_LINE_LEN},
        {P_LINE_DENSITY, P_LINE_MUTATE, P_LINE_RANGE, P_LINE_SLIDE},
        {P_EV_LEVEL, P_EV_DENSITY, P_EV_RUNS, P_EV_ECHO},
        {P_ATMOS_LEVEL, P_ATMOS_COLOUR, P_ATMOS_AIR, P_ATMOS_WASH},
        {P_ECHO_LEVEL, P_ECHO_FB, P_ECHO_TONE, P_ECHO_TIME},
        {-1, -1, -1, -1},
    };
    return PAR[clamp(p, 0, NPAGE - 1)][k & 3];
}
static const char *const LANE_NAME[NLANE] = {"KICK", "BASS", "PERC", "LINE", "EVENTS"};
static int page_lane(int p)              /* the lane a page acts on (the screen brightens it), or -1 */
{
    return p == PG_KICK ? LANE_KICK : p == PG_BASS ? LANE_BASS : p == PG_PERC ? LANE_PERC : p == PG_EVENTS ? LANE_EVENTS
         : p == PG_LINE || p == PG_LINE_SEQ ? LANE_LINE : -1;
}

static struct {
    char msg[2][32];
    uint32_t msg_until;
    uint32_t keys, btn;
    uint32_t turned_at[4];               /* each knob's last turn (the strip lights it) */
    uint32_t last_draw;
    uint32_t seen[NHIT];                 /* the engine's counts as last drawn */
    uint32_t struck_at[NLANE];           /* each lane's last note (the playhead's step flares) */
    uint32_t thrown_at;                  /* a key's event named on the screen since (0: none) */
    uint32_t save_at;                    /* SAVE pressed at (a cue: tapped or held) */
    uint8_t save_held;                   /* SAVE held long enough: the drop cued */
    uint32_t seq_at;                     /* SEQ pressed at (tapped: the pattern's page; held: the pattern anew) */
    uint8_t seq_held;
    uint8_t last_part;                   /* the part last shown (SEQ from a page without one) */
    int8_t thrown_type;
    uint8_t page;
    uint8_t redraw;
    int8_t oct;                          /* the keys' events' octave (OCT- / OCT+) */
    uint32_t logo_until;                 /* the logo stays until (the keys and knobs already work) */
} ui;
static int page_part(int p)              /* the part a page shows (PART_*), or -1 */
{
    switch (p) {
    case PG_KICK: return PART_KICK;
    case PG_BASS: return PART_BASS;
    case PG_PERC: return PART_PERC;
    case PG_LINE:
    case PG_LINE_SEQ: return PART_LINE;
    case PG_EVENTS: return PART_EVENTS;
    case PG_ATMOS: return PART_ATMOS;
    default: return -1;
    }
}
static int algo_part(void)               /* ALGORITHM's part: the page's (MAIN, GROOVE, PHASE, ECHO, GLOBAL: the bass, for now) */
{
    int p = page_part(ui.page);
    return p < 0 ? PART_BASS : p;
}

static int grid_mode(void) { return ui.page == PG_ECHO; }

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
    if (page_part(p) >= 0)
        ui.last_part = (uint8_t)page_part(p);
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

static void page_button(int b)           /* a page's button: its first page, or (on one of its pages) the next */
{
    int p, first = -1;
    for (p = 0; p < NPAGE; p++)
        if (PAGE_BTN[p] == b && first < 0)
            first = p;
    if (first < 0)
        return;
    p = ui.page + 1;
    page_go(PAGE_BTN[ui.page] == b && p < NPAGE && PAGE_BTN[p] == b ? p : first);
}

/* the knobs of a page: K1..K4's names, values and positions ("": none) */
static const char *knob_name(int page, int k)
{
    if (page == PG_GLOBAL)
        return k == 0 ? "LIGHTS" : k == 1 ? "THEME" : k == 2 ? "CPU" : "MIDI IN";
    return page_par(page, k) < 0 ? "" : ps_param(page_par(page, k))->name;
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
    else if (p >= P_BREAKS && p <= P_GAP && ps_phase_name(p, v))
        str_put(t, 12, ps_phase_name(p, v));
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
        } else if (k == 2) {             /* the audio's load, of its budget (read only; the device's) */
            uint32_t c = plat_cpu_pct();
            uint32_t k = 0;
            int_text((int32_t)c, t);
            while (t[k])
                k++;
            str_put(t + k, 4, ps_shedding() ? "%!" : "%");   /* (!: lighter, the render was near its time) */
            *pos = (int32_t)(c > 100u ? 100u : c);
            *span = 100;
        } else {
            str_put(t, 12, set.flags & SET_MIDI ? "ON" : "OFF");
            *pos = (set.flags & SET_MIDI) != 0;
            *span = 1;
        }
        return;
    }
    {
        int p = page_par(page, k);
        const ps_param_t *d;
        if (p < 0) {
            *t = 0;
            *pos = *span = 0;
            return;
        }
        d = ps_param(p);
        par_text(p, set.par[p], t);
        *pos = set.par[p] - d->lo;
        *span = d->hi - d->lo;
    }
}

static void knob_turn(int k, int32_t d)
{
    int page = ui.page;
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
        } else if (k == 3) {
            set.flags = (uint8_t)(d > 0 ? set.flags | SET_MIDI : set.flags & ~SET_MIDI);
            if (!(set.flags & SET_MIDI))
                midi_off();
        } else
            return;
    } else {
        int p = page_par(page, k);
        const ps_param_t *pi = ps_param(p < 0 ? 0 : p);
        int step = pi->hi - pi->lo >= 100 ? 2 : 1;   /* 0..100 two a detent, the rest (tempo, root, ...) one */
        int32_t v;
        if (p < 0)
            return;
        v = clamp(set.par[p] + step * d, pi->lo, pi->hi);
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

/* ------------------------------------------------------------ performance --- */
/* the keys as the performance: key k (0: F3) white (0..15 its place in the grid) or black (0..10) */
static int key_black(int k) { int pc = (k + 5) % 12; return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }
static int key_place(int k)              /* its place among the white keys, or among the black */
{
    int i, n = 0;
    for (i = 0; i < k; i++)
        n += key_black(i) == key_black(k);
    return n;
}
enum { BK_MUTE0 = 0, BK_FILL = 6, BK_NOKICK, BK_NOBASS, BK_NONE, BK_ALL, NBK };
static const char *const PART_NAME[NPART] = {"KICK", "BASS", "PERC", "LINE", "EVENTS", "ATMOS"};
static const char *const FX_ROW[4] = {"REPEAT", "FILTER", "SPACE", "MOTION"};

enum { ROLE_EVENT, ROLE_FX, ROLE_BLACK };
static struct {
    uint8_t role[NKEYS];                 /* what a key does, as it was pressed */
    uint16_t fx_held;
    uint8_t muted;                       /* as the UI asked (the engine's is ps_ui.muted) */
} pf;

static int grid_mode(void);

static void key_down(int k, int vel)
{
    if (!grid_mode()) {
        pf.role[k] = ROLE_EVENT;
        ps_key(k, 1, vel);
        return;
    }
    if (!key_black(k)) {
        int id = key_place(k);
        pf.role[k] = ROLE_FX;
        pf.fx_held |= (uint16_t)(1u << id);
        ps_fx(id, 1);
        app_say(FX_ROW[id / 4], ps_fx_name(id));
        return;
    }
    pf.role[k] = ROLE_BLACK;
    {
        int b = key_place(k), p;
        if (b < BK_FILL) {
            ps_mute(b);
            pf.muted ^= (uint8_t)(1u << b);
            app_say(PART_NAME[b], pf.muted >> b & 1u ? "MUTED" : "BACK");
        } else if (b == BK_FILL) {
            ps_fill();
            app_say("FILL", "NEXT BEAT");
        } else if (b == BK_NOKICK || b == BK_NOBASS) {
            ps_out(b == BK_NOKICK ? PART_KICK : PART_BASS, 1);
            app_say(b == BK_NOKICK ? "KICK" : "BASS", "OUT");
        } else if (b == BK_ALL) {
            for (p = 0; p < NPART; p++)
                if (pf.muted >> p & 1u)
                    ps_mute(p);
            pf.muted = 0;
            app_say("ALL", "BACK");
        }
    }
}

static void key_up(int k)
{
    if (pf.role[k] == ROLE_EVENT)
        ps_key(k, 0, 0);
    else if (pf.role[k] == ROLE_FX) {
        int id = key_place(k);
        pf.fx_held &= (uint16_t)~(1u << id);
        ps_fx(id, 0);
    } else {
        int b = key_place(k);
        if (b == BK_NOKICK || b == BK_NOBASS)
            ps_out(b == BK_NOKICK ? PART_KICK : PART_BASS, 0);
    }
}

/* ------------------------------------------------------------------- midi --- */
/* MIDI in (optional: GLOBAL's MIDI IN, off by default), from USB and the TRS jack: a note throws an event as its key
 * would (folded by octaves into the keys' F3..G5); start, continue and stop play and rest (start from the top); a
 * clock's tempo is followed (its beats' length, smoothed) until the clock stops for half a second */
static struct {
    uint32_t down;                       /* the keys a note holds */
    uint32_t last_t24, beat_t24;         /* the last clock's time; a beat's length (t24 ticks, smoothed) */
    uint32_t last_ms;
    uint8_t clocks;
} md;

static void midi_off(void)
{
    uint32_t k;
    for (k = 0; k < NKEYS; k++)
        if (md.down >> k & 1u && !(ui.keys >> k & 1u))
            ps_key((int)k, 0, 0);
    md.down = 0;
    md.clocks = 0;
    md.beat_t24 = 0;
    ps_ext_bpm(0.0f);
}

static void midi_poll(void)
{
    uint32_t pkt;
    int port;
    int took = 0;
    while (took++ < 48 && plat_midi_in(&pkt, &port)) {   /* (at most 48 a pass: the rest wait, the queue keeps room) */
        uint32_t st = pkt >> 8 & 0xFFu, d1 = pkt >> 16 & 0x7Fu, d2 = pkt >> 24 & 0x7Fu;
        if (!(set.flags & SET_MIDI))
            continue;
        if (st == 0xF8u) {               /* a clock: 24 a beat */
#ifdef PLAT_HOST
            uint32_t t = plat_ms() * 1000u * PLAT_TICKS_PER_US;   /* (the host's t24 is its real clock, not the script's) */
#else
            uint32_t t = plat_midi_in_t24();   /* as it landed (fm1.toml midi_stamp) */
#endif
            if (md.clocks && t - md.last_t24 < PLAT_TICKS_PER_US * 100000u) {   /* (a tenth of a second apart: anew) */
                uint32_t beat = (t - md.last_t24) * 24u;
                md.beat_t24 = md.beat_t24 ? (uint32_t)((int32_t)md.beat_t24 + ((int32_t)(beat - md.beat_t24) >> 4)) : beat;
                if (md.clocks >= 24u) {
                    float bpm = 60.0f * 1e6f * (float)PLAT_TICKS_PER_US / (float)md.beat_t24;
                    ps_ext_bpm(bpm < 125.0f ? 125.0f : bpm > 220.0f ? 220.0f : bpm);   /* (the buffers' range) */
                }
            } else
                md.clocks = 0;
            if (md.clocks < 255u)
                md.clocks++;
            md.last_t24 = t;
            md.last_ms = plat_ms();
        } else if (st == 0xFAu) {        /* start: from the top */
            if (ps_playing())
                ps_restart();
            ps_play(1);
        } else if (st == 0xFBu) {        /* continue */
            ps_play(1);
        } else if (st == 0xFCu) {        /* stop */
            ps_play(0);
        } else if ((st & 0xE0u) == 0x80u) {   /* a note on or off, any channel */
            int k = (int)d1 - 53;
            while (k < 0)
                k += 12;
            while (k >= (int)NKEYS)
                k -= 12;
            if ((st & 0xF0u) == 0x90u && d2) {
                md.down |= 1u << k;
                ps_key(k, 1, (int)d2);
            } else if (md.down >> k & 1u) {
                md.down &= ~(1u << k);
                if (!(ui.keys >> k & 1u))
                    ps_key(k, 0, 0);
            }
        }
    }
    if (md.clocks && plat_ms() - md.last_ms > 500u) {   /* the clock stopped: the world's tempo again */
        md.clocks = 0;
        md.beat_t24 = 0;
        ps_ext_bpm(0.0f);
    }
}

/* SEQ: a part's pattern. Its page (the line's for now; others not yet), tapped from the part's sound (or from a page
 * without a part: the last shown), again back to its sound; held, the pattern drawn anew from the next bar */
static int seq_page(int part) { return part == PART_LINE ? PG_LINE_SEQ : -1; }
static void seq_tap(void)
{
    int part = page_part(ui.page);
    if (part < 0)
        part = ui.last_part;
    if (seq_page(part) < 0)
        app_say(PART_NAME[part], "NO PATTERN YET");
    else if (ui.page == seq_page(part))
        page_go(part == PART_LINE ? PG_LINE : ui.page);
    else
        page_go(seq_page(part));
}
static void seq_new(void)
{
    int part = page_part(ui.page);
    if (part < 0)
        part = ui.last_part;
    if (part == PART_LINE) {
        ps_line_new();
        app_say("LINE", "NEW PATTERN");
    } else
        app_say(PART_NAME[part], "NO PATTERN YET");
}

/* ------------------------------------------------------------------ input --- */
static void input(void)
{
    uint32_t keys, btn = plat_buttons(), k, down, up;
    int32_t e;
    touch_poll();                            /* the sensed keys' presses, velocities and pressure */
    midi_poll();
    keys = touch_keys();
    down = btn & ~ui.btn;
    up = ui.btn & ~btn;
    for (k = 0; k < NKEYS; k++) {            /* the keys throw events, or play the performance (key 0 is F3) */
        uint32_t bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit))
            key_down((int)k, touch_vel(k));
        else if (!(keys & bit) && (ui.keys & bit))
            key_up((int)k);
        else if ((keys & bit) && pf.role[k] == ROLE_EVENT)
            ps_key((int)k, 2, touch_pressure(k));
    }
    if (down & 1u << B_PLAY) {
        ps_play(!ps_playing());
        app_say(ps_playing() ? "RESTING" : "PLAYING", "");   /* (it changes as the next block starts) */
    }
    if (down & 1u << B_SAVE) {
        ui.save_at = plat_ms();
        ui.save_held = 0;
        if (ui.page == PG_GLOBAL)
            save();
    }
    if (ui.page != PG_GLOBAL && ps_playing()) {   /* SAVE elsewhere: the sections (after HYP-1's) */
        if ((btn & 1u << B_SAVE) && !ui.save_held && plat_ms() - ui.save_at >= 500u) {
            ui.save_held = 1;
            ps_cue(2);
            app_say("DROP", "NEXT BAR");
        } else if ((up & 1u << B_SAVE) && !ui.save_held) {
            int sec = ps_section();
            if (sec == SEC_BUILD)
                app_say("BUILDING", "HOLD: DROP");
            else {
                ps_cue(1);
                app_say(sec == SEC_MAIN ? "BREAK" : "BUILD", "NEXT BAR");
            }
        }
    }
    if (down & 1u << B_REC) {                /* the keys' loop: a take anew while held */
        ps_rec(1);
        app_say("REC", "TWO BARS");
    }
    if (up & 1u << B_REC) {
        ps_rec(0);
        app_say("REC", ps_take() ? "LOOPING" : "CLEARED");
    }
    for (k = 0; k < NB; k++)                 /* the pages' buttons (SEQ's: below) */
        if (k != B_SEQ && down >> k & 1u)
            page_button((int)k);
    if (down & 1u << B_SEQ) {
        ui.seq_at = plat_ms();
        ui.seq_held = 0;
    }
    if ((btn & 1u << B_SEQ) && !ui.seq_held && plat_ms() - ui.seq_at >= 500u) {
        ui.seq_held = 1;
        seq_new();
    } else if ((up & 1u << B_SEQ) && !ui.seq_held)
        seq_tap();
    if ((e = plat_enc(EN_SELECT)) != 0)
        page_go((int)ui.page + (int)e);
    if ((e = plat_enc(EN_PRESET)) != 0)
        world_step(e);
    if ((e = plat_enc(EN_ALGO)) != 0) {     /* the type of the page's part (elsewhere, for now: the bass's colour) */
        static const char *const PNAME[NPART] = {"KICK", "BASS", "PERC", "LINE", "EVENTS", "ATMOS"};
        int part = algo_part(), n = ps_ntypes(part);
        if (n > 1) {
            int c = (ps_type_now(part) + (int)e % n + n) % n;
            ps_type(part, c);
            app_say(PNAME[part], ps_type_name(part, c));
        } else
            app_say(PNAME[part], "ONE TYPE");
    }
    if (down & (1u << B_OCTDN | 1u << B_OCTUP)) {
        char t[4];
        ui.oct = (int8_t)clamp(ui.oct + (down & 1u << B_OCTUP ? 1 : 0) - (down & 1u << B_OCTDN ? 1 : 0), -1, 2);
        ps_octave(ui.oct);
        t[0] = ui.oct < 0 ? '-' : '+';
        t[1] = (char)('0' + (ui.oct < 0 ? -ui.oct : ui.oct));
        t[2] = 0;
        app_say("OCTAVE", ui.oct ? t : "0");
    }
    for (k = 0; k < 4; k++)
        if ((e = plat_enc((int)(EN_K1 + k))) != 0)
            knob_turn((int)k, e);
    if (keys != ui.keys || btn != ui.btn)
        ui.redraw = 1;
    ui.keys = keys;
    ui.btn = btn;
    {   /* lit: the page's button, PLAY playing, SAVE unsaved; dim: the buttons that have a page or a use */
        uint32_t tick = ps_ui.tick, sec = (uint32_t)ps_ui.section;
        int save_lit = ui.page == PG_GLOBAL ? set_dirty
                       : sec == SEC_BREAK || ps_ui.cued ? 1
                       : sec == SEC_BUILD ? tick % PS_TPB < PS_TPB / 2u : 0;   /* a build: on the beats */
        uint32_t lit = btn | 1u << PAGE_BTN[ui.page] | (save_lit ? 1u << B_SAVE : 0u) | (ps_playing() ? 1u << B_PLAY : 0u) |
                       (ui.oct < 0 ? 1u << B_OCTDN : 0u) | (ps_take() ? 1u << B_REC : 0u) |
                       (ui.oct > 0 ? 1u << B_OCTUP : 0u);
        uint32_t dim = 1u << B_HOME | 1u << B_SEL | 1u << B_ENV | 1u << B_LFO | 1u << B_EDIT | 1u << B_FX | 1u << B_GLO |
                       1u << B_SEQ | 1u << B_REC | 1u << B_SAVE | 1u << B_PLAY | 1u << B_OCTDN | 1u << B_OCTUP, klit = keys, kdim = 0;
        if (grid_mode())                     /* the grid: each row's first key glows, a muted part's key is lit */
            for (k = 0; k < NKEYS; k++) {
                int pl = key_place((int)k);
                if (!key_black((int)k) && pl % 4 == 0)
                    kdim |= 1u << k;
                if (key_black((int)k) && pl < NPART && (ps_ui.muted >> pl & 1u))
                    klit |= 1u << k;
            }
        plat_lights(lit, klit, dim, kdim, set.lights);
    }
}

void app_input_only(void) { input(); }

/* ----------------------------------------------------------------- screen --- */
/* The colours: psychedelic on black. Each part its own neon hue; the hues sway together over 32 bars, shade a little
 * from step to step along a bar, sink (less saturated) in a break, turn faster and faster through a build and jump
 * half way round at a drop, coming back over two bars. Hues are 0..1535 (six sextants of 256). */
#define HUE_FULL 1536
enum { HUE_MAGENTA = 1365, HUE_VIOLET = 1173, HUE_BLUE = 981, HUE_CYAN = 789, HUE_ACID = 405, HUE_GOLD = 162 };
static const int16_t LANE_HUE[NLANE] = {HUE_MAGENTA, HUE_VIOLET, HUE_CYAN, HUE_ACID, HUE_GOLD};
static const int16_t PART_HUE[NPART] = {HUE_MAGENTA, HUE_VIOLET, HUE_CYAN, HUE_ACID, HUE_GOLD, HUE_BLUE};

static uint16_t hsv_lift(int32_t h, int32_t sat, int32_t v, int32_t lo);
static uint16_t hsv(int32_t h, int32_t sat, int32_t v) { return hsv_lift(h, sat, v, 0); }

/* hsv, and if the colour is darker than lo (luma 0..255: blue and violet are, at full value) mixed toward white until
 * it is not: text in any hue stays legible on black */
static uint16_t hsv_lift(int32_t h, int32_t sat, int32_t v, int32_t lo)
{
    int32_t f, p, q, t, r, g, b;
    h %= HUE_FULL;
    if (h < 0)
        h += HUE_FULL;
    f = h & 255;
    p = v * (255 - sat) / 255;
    q = v * (255 - sat * f / 255) / 255;
    t = v * (255 - sat * (255 - f) / 255) / 255;
    switch (h >> 8) {
    case 0: r = v, g = t, b = p; break;
    case 1: r = q, g = v, b = p; break;
    case 2: r = p, g = v, b = t; break;
    case 3: r = p, g = q, b = v; break;
    case 4: r = t, g = p, b = v; break;
    default: r = v, g = p, b = q; break;
    }
    {
        int32_t y = (54 * r + 183 * g + 19 * b) >> 8;   /* Rec. 709 luma */
        if (y < lo) {                    /* c + (255 - c) * k, k = (lo - y) / (255 - y) */
            int32_t k = (lo - y) * 256 / (255 - y);
            r += (255 - r) * k >> 8;
            g += (255 - g) * k >> 8;
            b += (255 - b) * k >> 8;
        }
    }
    return RGB(r, g, b);
}

static struct {
    int32_t shift;                       /* the hues' movement now */
    int32_t sat;                         /* their saturation now */
    uint8_t drops_seen;
    uint32_t drop_tick;                  /* the last drop's tick (its jump fades from there) */
} hue;

static void hue_update(void)             /* once a frame: the sway, the section's colour */
{
    uint32_t tick = ps_ui.tick, bar = 4u * PS_TPB;
    int32_t sh = (int32_t)(110.0f * fm_sin_turns((float)(tick % (32u * bar)) / (float)(32u * bar)));
    if (ps_ui.drops != hue.drops_seen) {
        hue.drops_seen = ps_ui.drops;
        hue.drop_tick = tick;
    }
    if (ps_ui.section == SEC_BUILD) {    /* turning, faster and faster */
        float p = ((float)ps_ui.sec_bar + (float)(tick % bar) / (float)bar) / (float)(ps_ui.sec_len ? ps_ui.sec_len : 4u);
        sh += (int32_t)(2.0f * HUE_FULL * p * p);
    }
    if (hue.drops_seen && tick - hue.drop_tick < 2u * bar) {   /* the drop: half way round, back over two bars */
        float k = 1.0f - (float)(tick - hue.drop_tick) / (float)(2u * bar);
        sh += (int32_t)(HUE_FULL / 2 * k * k);
    }
    hue.shift = sh;
    hue.sat = ps_ui.section == SEC_BREAK ? 130 : 225;
}

static uint16_t part_col(int32_t base, int32_t v) { return hsv(base + hue.shift, hue.sat, v); }
static uint16_t part_txt(int32_t base, int32_t v)   /* a label in its part's colour, lifted to read on black */
{
    return hsv_lift(base + hue.shift, hue.sat, v, v * 120 / 255);
}

/* a text, each character its own hue, from h on by dh */
static int32_t text_rainbow(int32_t x, int32_t y, const felucca_font_t *f, const char *t, int32_t h, int32_t dh, int32_t sat,
                            int32_t v)
{
    char c[2] = {0, 0};
    for (; *t; t++) {
        c[0] = *t;
        x = cv_text(x, y, f, c, hsv_lift(h, sat, v, v * 110 / 255));
        h += dh;
    }
    return x;
}

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
        if (ps_playing())                    /* the world's name, a rainbow drifting with the hues */
            text_rainbow(10, 6, &FONT_B, ps_world_name(set.world), HUE_MAGENTA + hue.shift, -70, 150, 255);
        else
            cv_text(10, 6, &FONT_B, ps_world_name(set.world), C_GRAY);
        int_text(ps_bpm(), t);
        x = 230 - text_w(&FONT_B, t) - 14;
        cv_text(x, 6, &FONT_B, t, C_WHITE);
        if (ps_playing())                    /* the beat: a lamp, its hue a step round each beat, white on the bar's */
            cv_rect(222, 12, 8, 8, tick % PS_TPB < 6u ? (beat == 0u ? C_WHITE : part_col(LANE_HUE[beat + 1u], 255))
                                                      : part_col(LANE_HUE[beat + 1u], 60));
    }
    cv_blit(0, 0);
}

/* what struck: a key's event named at once, a lane's step flaring as it sounds */
static void marks_update(void)
{
    static const uint8_t LANE_OF[NHIT] = {LANE_KICK, LANE_BASS, LANE_PERC, LANE_EVENTS, LANE_EVENTS, LANE_LINE};
    uint32_t h, now = plat_ms();
    if (ps_ui.hits[HIT_THROW] != ui.seen[HIT_THROW]) {
        ui.thrown_type = ps_ui.thrown_type;
        ui.thrown_at = now;
    }
    for (h = 0; h < NHIT; h++) {
        uint32_t c = ps_ui.hits[h];
        if (c != ui.seen[h]) {
            ui.seen[h] = c;
            ui.struck_at[LANE_OF[h]] = now;
        }
    }
}

/* the performance: the grid's sixteen (held: lit) and the parts (muted: dark) */
static void draw_grid(void)
{
    int32_t r, c, i;
    cv_begin(240, 148, C_BLACK);
    for (r = 0; r < 4; r++) {
        int32_t y = 6 + r * 24;
        cv_text(8, y + 4, &FONT_XS, FX_ROW[r], part_txt(LANE_HUE[r], 220));
        for (c = 0; c < 4; c++) {
            int id = (int)(r * 4 + c), on = pf.fx_held >> id & 1u;
            int32_t x = 56 + c * 46, tw = text_w(&FONT_XS, ps_fx_name(id));
            cv_rect(x, y, 44, 18, part_col(LANE_HUE[r] + c * 40, on ? 255 : c == 0 ? 105 : 60));
            cv_text(x + (44 - tw) / 2, y + 4, &FONT_XS, ps_fx_name(id), on ? C_BLACK : C_WHITE);
        }
    }
    for (i = 0, c = 8; i < NPART; i++) {     /* the parts, in the black keys' order (c: x) */
        int mu = ps_ui.muted >> i & 1u;
        int32_t tw = text_w(&FONT_XS, PART_NAME[i]);
        cv_text(c, 106, &FONT_XS, PART_NAME[i], mu ? C_LINE : part_txt(PART_HUE[i], 240));
        if (mu)
            cv_rect(c, 110, tw, 1, C_GRAY);
        c += tw + 10;
    }
    cv_text(8, 126, &FONT_XS, "BLACK: MUTE  FILL  OUT  ALL", C_DIM);
    cv_blit(0, 32);
}

/* the lanes: the bar now as the engine will play it (ps_lane), a step a cell in fours (the beats): a note a block,
 * its height its weight (the line: its pitch, a bar of a piano roll), a rest a dot on the line. The step playing is
 * white, a moment brighter as it sounds; the rest of the bar stays as it is (no trail). The page's lane amber, the
 * others grey, a muted one dark */
static void draw_lanes(uint32_t now)
{
    uint32_t tick = ps_ui.tick ? ps_ui.tick - 1u : 0u;
    int32_t lane, i, range = ps_line_range();
    int focus = page_lane(ui.page);
    uint8_t st[24];
    cv_begin(240, 148, C_BLACK);
    for (lane = 0; lane < NLANE; lane++) {
        int32_t y = 4 + lane * 21, base = y + 15, n = ps_lane((int)lane, st), per = n / 4;
        int f = lane == focus, muted = ps_ui.muted >> lane & 1u;   /* (the lanes in the parts' order) */
        int32_t cur = ps_playing() ? (int32_t)(n == 24 ? tick / 4u % 24u : tick / 6u % 16u) : -1;
        int flare = now - ui.struck_at[lane] < 90u;
        int32_t v = f ? 255 : 210;           /* the page's lane brightest */
        cv_text(8, y + 3, &FONT_XS, LANE_NAME[lane], muted ? C_LINE : part_txt(LANE_HUE[lane], f ? 255 : 210));
        for (i = 0; i < n; i++) {
            int32_t x = 62 + i * (n == 24 ? 20 : 30) / 3 + i / per * 2, w = n == 24 ? 5 : 8;
            int here = i == cur;
            /* along the bar the hue shades on, a little each step */
            uint16_t c = here ? (muted ? C_GRAY : C_WHITE) : muted ? C_LINE : part_col(LANE_HUE[lane] + i * 96 / per, v);
            if (!st[i]) {
                cv_rect(x, base - 1, w, 1, here ? C_GRAY : muted ? C_LINE : part_col(LANE_HUE[lane] + i * 96 / per, 55));
            } else if (lane == LANE_LINE) {
                int32_t h = (st[i] - 1) * 11 / (range > 0 ? range : 1);
                cv_rect(x, base - 3 - (h > 11 ? 11 : h), w, 3, c);
            } else {
                int32_t h = st[i] == 1 ? 5 : st[i] == 2 ? 9 : 13;
                if (here && flare && !muted)
                    h += 2;
                cv_rect(x, base - h, w, h, c);
            }
        }
    }
    {   /* the phrase's eight bars; in a break, its bars (eight shown, then counting on); in a build, its bars */
        int sec = ps_ui.section, sb = ps_ui.sec_bar, nb = sec == SEC_BUILD && ps_ui.sec_len ? ps_ui.sec_len : 8,
            inph = sec == SEC_BREAK ? sb % 8 : sb;
        static const char *const SEC_NAME[3] = {"PHRASE", "BREAK", "BUILD"};
        cv_text(8, 112, &FONT_XS, SEC_NAME[clamp(sec, 0, 2)], sec == SEC_MAIN ? C_GRAY : C_WHITE);
        for (i = 0; i < nb; i++) {           /* a bar a hue round the circle; the one playing lit, the rest faint */
            int now_bar = i == inph && ps_playing();
            cv_rect(62 + i * (168 / nb), now_bar ? 114 : 116, 168 / nb - 4, now_bar ? 6 : 3,
                    hsv(HUE_MAGENTA + hue.shift - i * HUE_FULL / nb, hue.sat, now_bar ? 255 : 70));
        }
        if (ps_ui.cued)                      /* a cue waiting for the next bar */
            cv_text(8, 126, &FONT_XS, ps_ui.cued == 2 ? "DROP >" : sec == SEC_MAIN ? "BREAK >" : "BUILD >", C_WHITE);
    }
    {   /* a key's event, named at the right; the rhythm a black key holds, named left of it */
        int32_t xr = 236;
        if (ui.thrown_at && now - ui.thrown_at < 900u && ui.thrown_type >= 0) {
            const char *n = ps_event_name(ui.thrown_type);
            int32_t w = text_w(&FONT_B, n);
            cv_rect(xr - w - 12, 126, w + 8, 20, C_LINE);
            cv_text(xr - w - 8, 128, &FONT_B, n, C_WHITE);
            xr -= w + 16;
        }
        if (ps_rhythm() >= 0 && !grid_mode()) {
            const char *n = ps_rhythm_name(ps_rhythm());
            int32_t w = text_w(&FONT_B, n);
            cv_rect(xr - w - 12, 126, w + 8, 20, C_LINE);
            cv_text(xr - w - 8, 128, &FONT_B, n, part_txt(LANE_HUE[LANE_EVENTS], 255));
        }
    }
    cv_blit(0, 32);
}

/* the rule over the knobs (after HYP-1's): at its left what needs saying (GLOBAL with something unsaved: SAVE keeps
 * it), else on a part's page its type (ALGORITHM chooses it); at its right the page's name, then a short bar a page,
 * the one shown lit, its button's other pages a little brighter */
static void draw_rule(void)
{
    const char *ctx = 0, *name = PAGE_NAME[ui.page];
    int32_t xd = 231 - NPAGE * 5, xt = xd - 6 - text_w(&FONT_XS, name), xs = 0, i;
    if (ui.btn & 1u << B_REC)
        ctx = "REC: THE KEYS, TWO BARS";
    else if (ui.page == PG_GLOBAL && set_dirty)
        ctx = "SAVE: KEEP IT";
    if (ctx)
        xs = cv_text(6, 0, &FONT_XS, ctx, C_AMB) + 6;
    else if (ui.page >= PG_KICK && ui.page <= PG_ATMOS && ps_ntypes(algo_part()) > 1) {   /* a part's page: its type */
        int part = algo_part();
        xs = cv_text(6, 0, &FONT_XS, "ALGO: ", C_AMB);
        xs = cv_text(xs, 0, &FONT_XS, ps_type_name(part, ps_type_now(part)), part_txt(PART_HUE[part], 255)) + 6;
    }
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
    int page = ui.page;
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
        if (span > 0) {                      /* the value: a run of hues, from the knob's own */
            int32_t len = w * pos / span, j;
            for (j = 0; j < len; j += 4)
                cv_rect(x + j, 51, len - j < 4 ? len - j : 4, 2,
                        lit ? C_WHITE : hsv(LANE_HUE[i] + hue.shift + j * 12, hue.sat, 255));
        }
    }
    cv_blit(0, 180);
}

static void draw(void)
{
    uint32_t now = plat_ms();
    draw_header(now);
    if (grid_mode())
        draw_grid();
    else
        draw_lanes(now);
    draw_knobs(now);
}

/* the logo: 犀 over 虎, each dot three pixels square, a rainbow downwards on black, the name small under them */
static void draw_logo(void)
{
    int32_t ch, y, x;
    lcd_fill(0, 0, 240, 240, RGB(0, 0, 0));
    for (ch = 0; ch < 2; ch++) {
        cv_begin(240, 3 * LOGO_N, RGB(0, 0, 0));
        for (y = 0; y < LOGO_N; y++)
            for (x = 0; x < LOGO_N; x++)
                if (LOGO[ch][y] >> (LOGO_N - 1 - x) & 1u)
                    cv_rect(120 - 3 * LOGO_N / 2 + 3 * x, 3 * y, 3, 3,   /* down the two: magenta to acid green */
                            hsv(HUE_MAGENTA - (ch * LOGO_N + y) * (HUE_MAGENTA - HUE_ACID) / (2 * LOGO_N), 230, 255));
        cv_blit(0, 22 + ch * (3 * LOGO_N + 6));
    }
    cv_begin(240, 16, RGB(0, 0, 0));
    text_rainbow((240 - text_w(&FONT_XS, "RHINOTIGER")) / 2, 2, &FONT_XS, "RHINOTIGER", HUE_MAGENTA, -96, 160, 200);
    cv_blit(0, 200);
}

#ifdef APP_SPLASH
void app_splash(void) { draw_logo(); }
#endif

void app_ui_init(void)
{
    draw_logo();                             /* (the device's splash already; the host's first sight of it) */
    ui.logo_until = plat_ms() + 1200u;
    ui.thrown_type = -1;
    ui.last_part = PART_LINE;
    ui.redraw = 1;
}

void app_frame(void)
{
    uint32_t now = plat_ms();
    input();
    ann_tick(now);
    marks_update();
    hue_update();
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (shed_at && now - shed_at > 8000u) {   /* lighter no more */
        shed_at = 0;
        ps_shed(0);
    }
    if (ui.logo_until) {                     /* the logo, a moment more */
        if ((int32_t)(now - ui.logo_until) < 0)
            return;
        ui.logo_until = 0;
        lcd_fill(0, 0, 240, 240, C_BLACK);
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
    else if (!strcmp(what, "page"))          /* 0 MAIN .. 11 GLOBAL (PG_*) */
        *got = ui.page;
    else if (!strcmp(what, "bar"))           /* bars since PLAY */
        *got = ps_bar();
    else if (!strcmp(what, "playing"))
        *got = ps_playing();
    else if (!strcmp(what, "dirty"))
        *got = set_dirty;
    else if (!strcmp(what, "section"))       /* 0 the groove, 1 a break, 2 a build */
        *got = ps_section();
    else if (!strcmp(what, "drops"))
        *got = ps_ui.drops;
    else if (!strcmp(what, "colour"))        /* the bass's colour, 0..3 */
        *got = ps_colour_now();
    else if (!strcmp(what, "line_type"))     /* the line's type, 0..3 */
        *got = ps_type_now(PART_LINE);
    else if (!strcmp(what, "events_type"))   /* the events' type, 0..3 */
        *got = ps_type_now(PART_EVENTS);
    else if (!strcmp(what, "atmos_type"))    /* the atmos's type, 0..3 */
        *got = ps_type_now(PART_ATMOS);
    else if (!strcmp(what, "perc_type"))     /* the percussion's type, 0..3 */
        *got = ps_type_now(PART_PERC);
    else if (!strcmp(what, "kick_type"))     /* the kick's type, 0..3 */
        *got = ps_type_now(PART_KICK);
    else if (!strcmp(what, "rhythm"))        /* the black key held whose rhythm leads (-1: none) */
        *got = ps_rhythm();
    else if (!strcmp(what, "throws"))        /* the keys' throws so far */
        *got = (int)ps_ui.hits[HIT_THROW];
    else if (!strcmp(what, "auto"))          /* breaks on their own (BREAKS above 0) */
        *got = set.par[P_BREAKS] > 0;
    else if (!strcmp(what, "take"))          /* the throws in the keys' loop */
        *got = ps_take();
    else if (!strcmp(what, "lines"))         /* the line's notes so far */
        *got = (int)ps_ui.hits[HIT_LINE];
    else if (!strcmp(what, "muted"))         /* the parts muted, 1 << PART_* (the engine's) */
        *got = ps_ui.muted;
    else if (!strcmp(what, "fx"))            /* the grid's effects held, 1 << id */
        *got = pf.fx_held;
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
