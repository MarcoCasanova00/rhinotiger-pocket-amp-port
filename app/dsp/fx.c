/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 hacha */
/* RhinoTiger's performance effects (psy.c includes this; psy.h ps_fx, ps_part): the sixteen of the white keys' grid,
 * each while its key is held, over the whole mix, and the black keys' parts (muted, or out while held) and fill.
 *
 *   REPEAT   1/4, a quarter triplet, 1/8, an eighth triplet: that much of the music, again and again (the quarters
 *            from the next beat, the eighths from the next sixteenth)
 *   FILTER   LOW and HIGH (the cutoff glides down, up), FLANGE (a short swept delay), BAND (a telephone's)
 *   SPACE    WASH (the music, but the kick and the bass, into a great hall: no repeats, a cloud round it, its own
 *            sound half sunk into it; let go, the music back, the hall dying away over a few seconds), DUB (into the
 *            echo, its return up, its own sound a third sunk, the echo darker, longer, its time stretching slowly, wavering
 *            like a tape's: its pitch sinking; let go, it springs back), FREEZE (the music above the kick and the bass
 *            taken into the echo for one echo's time, then looped there, no more in, its own sound sinking beneath),
 *            REVERSE (the last beat, backwards). FREEZE brings the echo's return up to most of its height (ECHO's
 *            LEVEL lower), falling back over ~2 s after
 *   MOTION   TAPE (the music slows to a stop over two beats), RISER (a band of noise rising over two bars), STAIRS (a
 *            sixteenth again and again, a minor third higher each time, up to two octaves), GATE (what is above the
 *            kick and the bass cut in sixteenths; their echo goes on)
 *
 * The buffer's effects (the repeats, REVERSE, TAPE, STAIRS) take turns: the last pressed plays, from the next
 * sixteenth (the quarters: beat; the one before fading out over 3 ms beneath it). The rest add up. Each fades in and out
 * in a few milliseconds. The buffer: the last 1.02 s of the mix (two beats at 125 bpm, the slowest a world goes; a
 * clock slower still: the slice as long as it holds), in the pool; while a buffer's effect replays what it took, the
 * buffer keeps it (no more is written, and its count stands: what is behind it stays the music as it went). */

enum { FX_R4, FX_R4T, FX_R8, FX_R8T, FX_LOW, FX_HIGH, FX_FLANGE, FX_BAND,
       FX_WASH, FX_DUB, FX_FREEZE, FX_REVERSE, FX_TAPE, FX_RISER, FX_STAIRS, FX_GATE, FX_N,
       FX_AUTO = FX_N };               /* (the buffer's own: a STUTTER build's repeat, auto_slice; under a key's) */
static const char *const FX_NAME[FX_N] = {"1/4", "1/4T", "1/8", "1/8T", "LOW", "HIGH", "FLANGE", "BAND",
                                          "WASH", "DUB", "FREEZE", "REVERSE", "TAPE", "RISER", "STAIRS", "GATE"};

#define CAP_N 45056u                   /* frames: 1.02 s (two beats at 125 bpm are 42336) */
static PLAT_POOL int16_t cap_buf[2][CAP_N];
#define FL_N 512u                      /* FLANGE's own short ring: always written, held buffer or not */
static int16_t fl_buf[2][FL_N];
static uint32_t fl_w;
static float fl_read(int ch, float d)  /* d frames ago, between two samples */
{
    uint32_t i = (uint32_t)d;
    float f = d - (float)i, a = (float)fl_buf[ch][(fl_w - 1u - i) % FL_N], b = (float)fl_buf[ch][(fl_w - 2u - i) % FL_N];
    return (a + (b - a) * f) * (2.0f / 32767.0f);
}

typedef struct {                       /* a buffer's effect playing */
    int8_t id;                         /* FX_* (-1 none) */
    uint32_t p0;                       /* where it began (a frame count) */
    float el;                          /* frames since it began */
    float tape_pos;                    /* TAPE: frames read since it began */
    float slice;                       /* FX_AUTO: its slice (ticks) */
} bufv_t;

static struct {
    uint16_t held;                     /* the grid's keys held (FX_* bits) */
    int8_t buf_want;                   /* the buffer's effect pressed (-1 none) */
    uint8_t buf_on;                    /* the one playing is held (0: fading out) */
    uint8_t cap_hold;                  /* the buffer keeps what it has: a buffer's effect replays it */
    bufv_t cur, old;                   /* the buffer's effect playing, and the one it took over from (fading) */
    float xf;                          /* the old one's share (1 to 0 over ~3 ms) */
    uint32_t cap_w;                    /* frames written */
    float wet;                         /* the buffer's effect's share (glides) */
    float lo_g, hi_g, band_g, fl_g;    /* the filters' depths (glide, 0..1) */
    float svf_lo[2][2], svf_hi[2][2], svf_bp[2][2];   /* low, band per channel */
    float fl_ph;
    float riser_g, riser_t, rz[2];
    float gate_g, gate_s, six_el;      /* GATE: its depth (glides), the cut heard, frames since the sixteenth */
    float wash_g, frz_g, dub_g;        /* WASH, FREEZE, DUB: how far the music above the kick and the bass has sunk (glide) */
    float ret_lift;                    /* FREEZE, DUB: the echo's return at least this (falls back over ~2 s) */
    uint32_t wash_n;                   /* WASH's hall: frames it still sounds (0: quiet, cleared, not worked) */
    float lo_f, hi_f, bp_f, rz_f;      /* worked out as a block begins (fx_block) */
    uint8_t muted;                     /* the parts muted (PART_* bits) */
    uint8_t out;                       /* the parts out while a key is held */
    uint8_t fill_req;                  /* a fill cued for the next beat */
    uint8_t half;                      /* HALF held: the kick and the bass in half time */
    uint8_t gap_req;                   /* GAP cued: the next bar's last beat to come silent */
    uint16_t auto_held;                /* AUTO PLAY's (hands.c, worked out each tick): its effects, */
    int8_t auto_key;                   /* its buffer's effect (-1 none), */
    uint8_t auto_out, auto_half;       /* its parts out, its half time */
    uint8_t auto_slice;                /* a STUTTER build's repeat, ticks (0: none): psy.c sets it a tick */
    uint8_t auto_buf;                  /* a build's ending's buffer effect, FX_TAPE or FX_REVERSE (0: none): so too */
    uint8_t auto_dub;                  /* a build's ending thrown into the echo, as DUB held: so too */
    uint8_t auto_gate, ag_len;         /* a GATE build's slice, ticks (0: none): so too; the latest's */
    float ag_el, ag_g, ag_s;           /* frames since its slice began (psy.c: 0 as it does), its depth, the cut heard */
} fx = {.buf_want = -1, .auto_key = -1, .cur = {.id = -1}, .old = {.id = -1}};

/* the read: frame position p (a count, fractional), channel ch, between two samples */
static int fx_on(int id) { return (int)((uint32_t)(fx.held | fx.auto_held) >> id & 1u); }   /* held, or AUTO PLAY's */
static int dub_on(void) { return fx_on(FX_DUB) || fx.auto_dub; }   /* DUB: held, or an ending's */

static float cap_read(int ch, float p)
{
    uint32_t i = (uint32_t)p;
    float f = p - (float)i, a = (float)cap_buf[ch][i % CAP_N], b = (float)cap_buf[ch][(i + 1u) % CAP_N];
    return (a + (b - a) * f) * (2.0f / 32767.0f);
}

static void fx_cmd(int id, int on)     /* (render: from the command queue) */
{
    if (id < 0 || id >= FX_N)
        return;
    if (on) {
        fx.held |= (uint16_t)(1u << id);
        if (id <= FX_R8T || id == FX_REVERSE || id == FX_TAPE || id == FX_STAIRS)
            fx.buf_want = (int8_t)id;  /* from the next sixteenth */
        if (id == FX_RISER)
            fx.riser_t = 0.0f;
    } else {
        fx.held &= (uint16_t)~(1u << id);
        if (fx.buf_want == id)
            fx.buf_want = -1;
        if (fx.cur.id == id)           /* let go: back to the music (a buffer's effect still held takes over) */
            fx.buf_on = 0;
        if (fx.buf_want < 0) {
            int k;
            for (k = FX_STAIRS; k >= 0; k--)
                if ((fx.held >> k & 1u) && (k <= FX_R8T || k == FX_REVERSE || k == FX_TAPE || k == FX_STAIRS)) {
                    fx.buf_want = (int8_t)k;
                    break;
                }
        }
    }
}

static void fx_start(int id, float slice)   /* a buffer's effect from here (another playing fades out beneath it) */
{
    if (fx.cur.id >= 0) {
        fx.old = fx.cur;
        fx.xf = 1.0f;
    }
    fx.cur.id = (int8_t)id;
    fx.cur.p0 = fx.cap_w;
    fx.cur.el = 0.0f;
    fx.cur.tape_pos = 0.0f;
    fx.cur.slice = slice;
    fx.buf_on = 1;
    fx.cap_hold = 0;                   /* the new one's first time through is written as it plays */
}

static void fx_sixteenth(int beat)     /* a sixteenth begins (beat: a beat too): a pressed buffer's effect starts here */
{
    fx.six_el = 0.0f;
    if (fx.buf_want >= 0) {            /* a key's */
        if ((fx.buf_want != fx.cur.id || !fx.buf_on) && (beat || fx.buf_want > FX_R4T))
            fx_start(fx.buf_want, 0.0f);
    } else if (fx.auto_buf) {          /* a build's ending's (TAPE, REVERSE): from where it begins, on */
        if (fx.cur.id != (int)fx.auto_buf || !fx.buf_on)
            fx_start(fx.auto_buf, 0.0f);
    } else if (fx.auto_slice) {        /* a STUTTER build's: anew as its slice shortens (a quarter's from a beat) */
        if ((fx.cur.id != FX_AUTO || !fx.buf_on || fx.cur.slice != (float)fx.auto_slice) && (beat || fx.auto_slice < 24u))
            fx_start(FX_AUTO, (float)fx.auto_slice);
    } else if (fx.auto_key >= 0) {     /* AUTO PLAY's */
        if (fx.cur.id != fx.auto_key || !fx.buf_on)
            fx_start(fx.auto_key, 0.0f);
    } else if (fx.cur.id >= 0 && fx.buf_on)   /* its breath, its ending done (the drop): back to the music */
        fx.buf_on = 0;
}

/* a buffer's effect, a frame: its read into l and r; *hold set when it replays what the buffer took */
static void fx_buffer(bufv_t *v, float tick_len, float *l, float *r, int *hold)
{
    static const float SLICE[4] = {24.0f, 16.0f, 12.0f, 8.0f};   /* ticks: 1/4, a quarter triplet, 1/8, an eighth
                                                                  * triplet */
    int id = v->id;
    float len, u, p, w = 1.0f, base = (float)(v->p0 % CAP_N);
    *hold = 0;
    if (id <= FX_R8T || id == FX_STAIRS || id == FX_AUTO) {
        len = fm_minf(tick_len * (id == FX_STAIRS ? 6.0f : id == FX_AUTO ? v->slice : SLICE[id]), (float)(CAP_N - 256u));
        u = v->el - len * fm_floorf(v->el / len);
        w = fm_minf(u, len - u);       /* the slice's edges, softened */
        if (id == FX_STAIRS) {         /* each time round a minor third higher, up to two octaves */
            int k = (int)(v->el / len);
            float q = u * fm_exp2f((float)(k > 8 ? 8 : k) * 3.0f / 12.0f);
            q -= len * fm_floorf(q / len);
            w = fm_minf(w, fm_minf(q, len - q));   /* (its read wrapping inside the slice: softened too) */
            p = base + q;
        } else {
            p = base + u;
        }
        w = fm_clampf(w / 64.0f, 0.0f, 1.0f);
        if (v->el < len)               /* the first time through: the music itself, written */
            w = 1.0f;
        *hold = v->el + 1.0f >= len;
    } else if (id == FX_REVERSE) {     /* the last beat backwards, again and again */
        len = fm_minf(tick_len * (float)PS_TPB, (float)(CAP_N - 256u));
        u = v->el - len * fm_floorf(v->el / len);
        p = base + (float)CAP_N - 1.0f - u;
        w = fm_clampf(fm_minf(u, len - u) / 64.0f, 0.0f, 1.0f);
        *hold = 1;
    } else {                           /* TAPE: slowing to a stop over two beats (its read a beat behind at the end:
                                        * inside the buffer, which goes on being written) */
        float rate = 1.0f - v->el / (tick_len * (float)(2u * PS_TPB));
        if (rate < 0.0f)
            rate = 0.0f;
        v->tape_pos += rate;
        p = base + v->tape_pos;
        w = rate < 0.05f ? rate / 0.05f : 1.0f;
    }
    v->el += 1.0f;
    *l = cap_read(0, p) * w;
    *r = cap_read(1, p) * w;
}

static void fx_block(float tick_len)   /* as a block begins: the filters' coefficients, the sweeps' rates */
{
    float two = tick_len * (float)(8u * PS_TPB);   /* two bars, in frames */
    fx.lo_f = 2.0f * fm_sinf(3.14159265f * 250.0f * fm_exp2f(5.0f * (1.0f - fx.lo_g)) / PS_SR);   /* 8 kHz down */
    fx.hi_f = 2.0f * fm_sinf(3.14159265f * 1600.0f * fm_exp2f(-5.0f * (1.0f - fx.hi_g)) / PS_SR);  /* 50 Hz up */
    fx.bp_f = 2.0f * fm_sinf(3.14159265f * 1300.0f / PS_SR);
    fx.rz_f = 2.0f * fm_sinf(3.14159265f * 250.0f * fm_exp2f(4.7f * fm_clampf(fx.riser_t / two, 0.0f, 1.0f)) / PS_SR);
}

static void svf(float st[2], float x, float f, float q)   /* Chamberlin: st[0] low, st[1] band */
{
    float hi = x - st[0] - q * st[1];
    st[1] += f * hi;
    st[0] += f * st[1];
}

/* WASH's hall: four lines feeding back through a Householder matrix (each line into all), damped, the input spread by
 * two all-passes a side first; its tail ~6 s (RT60), its lines int16 (27 KB) */
#define WV_RT60 6.0f
#define WASH_RET 1.3f                  /* the hall's return */
static const uint16_t WV_LEN[4] = {2423, 3011, 3709, 4421};   /* (55 to 100 ms, prime) */
static const uint16_t WD_LEN[4] = {223, 557, 241, 593};      /* the all-passes: left two, right two */
static int16_t wv_buf[2423 + 3011 + 3709 + 4421];
static float wd_buf[223 + 557 + 241 + 593], wv_lp[4], wv_fb[4];
static uint16_t wv_pos[4], wd_pos[4];
static void wash_clear(void)
{
    uint32_t k;
    for (k = 0; k < sizeof wv_buf / sizeof wv_buf[0]; k++)
        wv_buf[k] = 0;
    for (k = 0; k < sizeof wd_buf / sizeof wd_buf[0]; k++)
        wd_buf[k] = 0.0f;
    for (k = 0; k < 4u; k++)
        wv_lp[k] = 0.0f;
}
static void wash(float in_l, float in_r, float *out_l, float *out_r)
{
    float y[4], sum, x[2] = {in_l, in_r};
    int16_t *b = wv_buf;
    float *a = wd_buf;
    int k, c;
    if (wv_fb[0] == 0.0f)              /* (the lines' gains for the tail: once) */
        for (k = 0; k < 4; k++)
            wv_fb[k] = fm_exp2f(-3.0f * 3.3219281f * (float)WV_LEN[k] / (WV_RT60 * PS_SR));
    for (c = 0; c < 2; c++)            /* the input spread */
        for (k = c * 2; k < c * 2 + 2; k++) {
            float d = a[wd_pos[k]], o = d - 0.6f * x[c];
            a[wd_pos[k]] = x[c] + 0.6f * o;
            if (++wd_pos[k] >= WD_LEN[k])
                wd_pos[k] = 0;
            a += WD_LEN[k];
            x[c] = o;
        }
    for (k = 0, b = wv_buf; k < 4; b += WV_LEN[k], k++)
        y[k] = (float)b[wv_pos[k]] * (1.0f / 32768.0f);
    sum = 0.5f * (y[0] + y[1] + y[2] + y[3]);
    for (k = 0, b = wv_buf; k < 4; b += WV_LEN[k], k++) {
        float f = y[k] - sum;          /* (Householder: each into all, the energy kept) */
        wv_lp[k] += (f - wv_lp[k]) * 0.42f;   /* (damped: the tail darkening, ~3 kHz) */
        b[wv_pos[k]] = (int16_t)(fm_clampf(wv_lp[k] * wv_fb[k] + x[k & 1] * 0.5f, -1.0f, 1.0f) * 32767.0f);
        if (++wv_pos[k] >= WV_LEN[k])
            wv_pos[k] = 0;
    }
    *out_l = y[0] + y[2];
    *out_r = y[1] + y[3];
}

/* the effects over the mix, a frame: l and r in place; what goes to the echo returned (DUB); n a noise */
static float fx_render(float tick_len, float *l, float *r, float upper_l, float upper_r, float n)
{
    float dl = *l, dr = *r, bl = 0.0f, br = 0.0f, send = 0.0f, g;
    fl_buf[0][fl_w % FL_N] = (int16_t)(fm_clampf(dl * 0.5f, -1.0f, 1.0f) * 32767.0f);
    fl_buf[1][fl_w % FL_N] = (int16_t)(fm_clampf(dr * 0.5f, -1.0f, 1.0f) * 32767.0f);
    fl_w++;
    /* the buffer keeps the mix, unless a buffer's effect is replaying it */
    if (!fx.cap_hold) {
        cap_buf[0][fx.cap_w % CAP_N] = (int16_t)(fm_clampf(dl * 0.5f, -1.0f, 1.0f) * 32767.0f);
        cap_buf[1][fx.cap_w % CAP_N] = (int16_t)(fm_clampf(dr * 0.5f, -1.0f, 1.0f) * 32767.0f);
    }
    if (fx.cur.id >= 0) {
        int hold, h2;
        fx_buffer(&fx.cur, tick_len, &bl, &br, &hold);
        if (fx.old.id >= 0) {          /* the one taken over from, fading out beneath */
            float ol, orr;
            fx_buffer(&fx.old, tick_len, &ol, &orr, &h2);
            bl += (ol - bl) * fx.xf;
            br += (orr - br) * fx.xf;
            if ((fx.xf -= 0.008f) <= 0.0f)
                fx.old.id = -1;
        }
        if (!fx.cap_hold)
            fx.cap_w++;
        fx.cap_hold = (uint8_t)hold;
        fx.wet += ((fx.buf_on ? 1.0f : 0.0f) - fx.wet) * 0.01f;
        dl += (bl - dl) * fx.wet;
        dr += (br - dr) * fx.wet;
        if (!fx.buf_on && fx.wet < 0.0005f) {   /* faded out: the music again, written again */
            fx.cur.id = fx.old.id = -1;
            fx.cap_hold = 0;
        }
    } else {
        if (!fx.cap_hold)
            fx.cap_w++;
        fx.wet = 0.0f;
    }
    /* GATE: what is above the kick and the bass (the buffer's replay, all of it), open the first half of each
     * sixteenth; the edges ~0.75 ms slopes */
    fx.gate_g += ((fx_on(FX_GATE) ? 1.0f : 0.0f) - fx.gate_g) * 0.003f;
    fx.six_el += 1.0f;
    if (fx.gate_g > 0.0005f) {
        float cut;
        fx.gate_s += ((fx.six_el < tick_len * 3.0f ? 0.0f : 1.0f) - fx.gate_s) * 0.03f;
        cut = fx.gate_g * fx.gate_s;
        dl -= cut * (dl - (1.0f - fx.wet) * (*l - upper_l));   /* (*l: the music as it came; less its upper, the kick
                                                                 * and the bass, untouched unless replayed) */
        dr -= cut * (dr - (1.0f - fx.wet) * (*r - upper_r));
    }
    /* a GATE build's: all of it, open the first half of each slice; ~0.75 ms slopes */
    fx.ag_g += ((fx.auto_gate ? 1.0f : 0.0f) - fx.ag_g) * 0.003f;
    fx.ag_el += 1.0f;
    if (fx.ag_g > 0.0005f) {
        fx.ag_s += ((fx.ag_el < tick_len * 0.5f * (float)fx.ag_len ? 0.0f : 1.0f) - fx.ag_s) * 0.03f;
        dl *= 1.0f - fx.ag_g * fx.ag_s;
        dr *= 1.0f - fx.ag_g * fx.ag_s;
    }
    /* WASH and FREEZE (its taking in is psy.c's): what is above the kick and the bass sinking beneath the hall, the
     * echo (~10 ms down, ~40 ms back; a buffer's replay, all of it, stays) */
    {
        int wa = fx_on(FX_WASH), fz = fx_on(FX_FREEZE), du = dub_on();
        fx.wash_g += ((wa ? 1.0f : 0.0f) - fx.wash_g) * (wa ? 0.002f : 0.0006f);
        fx.frz_g += ((fz ? 1.0f : 0.0f) - fx.frz_g) * (fz ? 0.002f : 0.0006f);
        fx.dub_g += ((du ? 1.0f : 0.0f) - fx.dub_g) * (du ? 0.002f : 0.0006f);
        fx.ret_lift = fz || du ? 0.9f : fx.ret_lift * 0.999985f;   /* (~1.5 s to a third) */
        if (fx.wash_g > 0.0005f || fx.frz_g > 0.0005f || fx.dub_g > 0.0005f) {
            float sink = (0.6f * fx.wash_g + 0.6f * fx.frz_g + 0.35f * fx.dub_g) * (1.0f - fx.wet) *
                         (1.0f - fx.gate_g * fx.gate_s);   /* (GATE's cut already gone) */
            if (sink > 0.75f)
                sink = 0.75f;
            dl -= sink * upper_l;
            dr -= sink * upper_r;
        }
        if (wa)                        /* the hall: sounding while held and ~8 s after, then cleared and still */
            fx.wash_n = 8u * (uint32_t)PS_SR;
        if (fx.wash_n) {
            float wl, wr;
            wash(upper_l * fx.wash_g, upper_r * fx.wash_g, &wl, &wr);
            dl += WASH_RET * wl;
            dr += WASH_RET * wr;
            if (!--fx.wash_n)
                wash_clear();
        }
    }
    /* the filters: LOW down to 250 Hz, HIGH up to 1.6 kHz, BAND a telephone, FLANGE */
    fx.lo_g += ((fx_on(FX_LOW) ? 1.0f : 0.0f) - fx.lo_g) * (fx_on(FX_LOW) ? 0.00006f : 0.0003f);
    fx.hi_g += ((fx_on(FX_HIGH) ? 1.0f : 0.0f) - fx.hi_g) * (fx_on(FX_HIGH) ? 0.00006f : 0.0003f);
    fx.band_g += ((fx_on(FX_BAND) ? 1.0f : 0.0f) - fx.band_g) * 0.002f;
    fx.fl_g += ((fx_on(FX_FLANGE) && !shed ? 1.0f : 0.0f) - fx.fl_g) * 0.002f;   /* (lighter: none) */
    if (fx.lo_g > 0.001f) {
        float f = fx.lo_f;
        svf(fx.svf_lo[0], dl, f, 0.6f);
        svf(fx.svf_lo[1], dr, f, 0.6f);
        g = fx.lo_g > 0.05f ? 1.0f : fx.lo_g / 0.05f;
        dl += (fx.svf_lo[0][0] - dl) * g;
        dr += (fx.svf_lo[1][0] - dr) * g;
    }
    if (fx.hi_g > 0.001f) {
        float f = fx.hi_f;
        svf(fx.svf_hi[0], dl, f, 0.6f);
        svf(fx.svf_hi[1], dr, f, 0.6f);
        g = fx.hi_g > 0.05f ? 1.0f : fx.hi_g / 0.05f;
        dl -= fx.svf_hi[0][0] * g;
        dr -= fx.svf_hi[1][0] * g;
    }
    if (fx.band_g > 0.001f) {
        float f = fx.bp_f;
        svf(fx.svf_bp[0], dl, f, 0.5f);
        svf(fx.svf_bp[1], dr, f, 0.5f);
        dl += (fm_tanhf(1.6f * fx.svf_bp[0][1]) - dl) * fx.band_g;
        dr += (fm_tanhf(1.6f * fx.svf_bp[1][1]) - dr) * fx.band_g;
    }
    if (fx.fl_g > 0.001f) {            /* a 0.6 to 5 ms delay, swept once in two bars-ish (0.4 Hz) */
        float d;
        if ((fx.fl_ph += 0.4f / PS_SR) >= 1.0f)
            fx.fl_ph -= 1.0f;
        d = 26.0f + 190.0f * (0.5f + 0.5f * fm_sin_turns(fx.fl_ph));
        dl += 0.7f * fx.fl_g * fl_read(0, d);
        dr += 0.7f * fx.fl_g * fl_read(1, d * 1.07f);
        dl *= 1.0f - 0.3f * fx.fl_g;
        dr *= 1.0f - 0.3f * fx.fl_g;
    }
    /* DUB: what is above the kick and the bass, into the echo */
    if (fx_on(FX_DUB))
        send += 1.1f * (upper_l + upper_r);
    else if (fx.auto_dub)              /* an ending's: all of it (a KICK build's roll too), the kick and the bass less */
        send += 1.8f * (upper_l + upper_r) + 0.5f * (*l - upper_l + *r - upper_r);
    /* RISER: a band of noise, rising over two bars while held */
    fx.riser_g += ((fx_on(FX_RISER) ? 1.0f : 0.0f) - fx.riser_g) * 0.003f;
    if (fx.riser_g > 0.0005f) {
        float p = fm_clampf(fx.riser_t / (tick_len * (float)(8u * PS_TPB)), 0.0f, 1.0f), f, x;
        fx.riser_t += 1.0f;
        f = fx.rz_f;
        svf(fx.rz, n, f, 0.35f);
        x = fx.rz[1] * fx.riser_g * (0.08f + 0.3f * p * p);
        dl += x;
        dr += x;
        send += x;
    }
    *l = dl;
    *r = dr;
    return send;
}
