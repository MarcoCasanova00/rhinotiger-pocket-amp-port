/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 hacha */
/* RhinoTiger's performance effects (psy.c includes this; psy.h ps_fx, ps_part): the sixteen of the white keys' grid,
 * each while its key is held, over the whole mix, and the black keys' parts (muted, or out while held) and fill.
 *
 *   REPEAT   1/8, 1/16, 1/32, a sixteenth triplet: from the next sixteenth, that much of the music, again and again
 *   FILTER   LOW and HIGH (the cutoff glides down, up), FLANGE (a short swept delay), BAND (a telephone's)
 *   SPACE    THROW (the music, but the kick and the bass, into the echo), WASH (the same, darker, longer), FREEZE (the
 *            echo keeps what it holds, no more in), REVERSE (the last beat, backwards)
 *   MOTION   TAPE (the music slows to a stop over a beat), RISER (a band of noise rising over two bars), STAIRS (a
 *            sixteenth again and again, a whole tone higher each time), SPIN (left and right, quicker and quicker)
 *
 * The buffer's effects (the repeats, REVERSE, TAPE, STAIRS) take turns: the last pressed plays, from the next
 * sixteenth (the one before fading out over 3 ms beneath it). The rest add up. Each fades in and out in a few
 * milliseconds. The buffer: the last 0.74 s of the mix, in the pool; while a buffer's effect replays what it took, the
 * buffer keeps it (no more is written, and its count stands: what is behind it stays the music as it went). */

enum { FX_R8, FX_R16, FX_R32, FX_R16T, FX_LOW, FX_HIGH, FX_FLANGE, FX_BAND,
       FX_THROW, FX_WASH, FX_FREEZE, FX_REVERSE, FX_TAPE, FX_RISER, FX_STAIRS, FX_SPIN, FX_N };
static const char *const FX_NAME[FX_N] = {"1/8", "1/16", "1/32", "1/16T", "LOW", "HIGH", "FLANGE", "BAND",
                                          "THROW", "WASH", "FREEZE", "REVERSE", "TAPE", "RISER", "STAIRS", "SPIN"};

#define CAP_N 32768u                   /* frames: 0.74 s (a beat at 125 bpm is 21168) */
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
    float spin_g, spin_t, spin_ph;
    float lo_f, hi_f, bp_f, rz_f, spin_rate;   /* worked out as a block begins (fx_block) */
    uint8_t muted;                     /* the parts muted (PART_* bits) */
    uint8_t out;                       /* the parts out while a key is held */
    uint8_t fill_req;                  /* a fill cued for the next beat */
} fx = {.buf_want = -1, .cur = {.id = -1}, .old = {.id = -1}};

/* the read: frame position p (a count, fractional), channel ch, between two samples */
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
        if (id <= FX_R16T || id == FX_REVERSE || id == FX_TAPE || id == FX_STAIRS)
            fx.buf_want = (int8_t)id;  /* from the next sixteenth */
        if (id == FX_RISER)
            fx.riser_t = 0.0f;
        if (id == FX_SPIN)
            fx.spin_t = 0.0f;
    } else {
        fx.held &= (uint16_t)~(1u << id);
        if (fx.buf_want == id)
            fx.buf_want = -1;
        if (fx.cur.id == id)           /* let go: back to the music (a buffer's effect still held takes over) */
            fx.buf_on = 0;
        if (fx.buf_want < 0) {
            int k;
            for (k = FX_STAIRS; k >= 0; k--)
                if ((fx.held >> k & 1u) && (k <= FX_R16T || k == FX_REVERSE || k == FX_TAPE || k == FX_STAIRS)) {
                    fx.buf_want = (int8_t)k;
                    break;
                }
        }
    }
}

static void fx_sixteenth(void)         /* a sixteenth begins: a pressed buffer's effect starts here */
{
    if (fx.buf_want >= 0 && (fx.buf_want != fx.cur.id || !fx.buf_on)) {
        if (fx.cur.id >= 0) {          /* another playing: it fades out beneath the new one */
            fx.old = fx.cur;
            fx.xf = 1.0f;
        }
        fx.cur.id = fx.buf_want;
        fx.cur.p0 = fx.cap_w;
        fx.cur.el = 0.0f;
        fx.cur.tape_pos = 0.0f;
        fx.buf_on = 1;
        fx.cap_hold = 0;               /* the new one's first time through is written as it plays */
    }
}

/* a buffer's effect, a frame: its read into l and r; *hold set when it replays what the buffer took */
static void fx_buffer(bufv_t *v, float tick_len, float *l, float *r, int *hold)
{
    static const float SLICE[4] = {12.0f, 6.0f, 3.0f, 4.0f};   /* ticks: 1/8, 1/16, 1/32, a sixteenth triplet */
    int id = v->id;
    float len, u, p, w = 1.0f, base = (float)(v->p0 % CAP_N);
    *hold = 0;
    if (id <= FX_R16T || id == FX_STAIRS) {
        len = tick_len * (id == FX_STAIRS ? 6.0f : SLICE[id]);
        u = v->el - len * fm_floorf(v->el / len);
        w = fm_minf(u, len - u);       /* the slice's edges, softened */
        if (id == FX_STAIRS) {         /* each time round a whole tone higher, up to an octave and a fifth */
            int k = (int)(v->el / len);
            float q = u * fm_exp2f((float)(k > 10 ? 10 : k) * 2.0f / 12.0f);
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
        len = tick_len * (float)PS_TPB;
        u = v->el - len * fm_floorf(v->el / len);
        p = base + (float)CAP_N - 1.0f - u;
        w = fm_clampf(fm_minf(u, len - u) / 64.0f, 0.0f, 1.0f);
        *hold = 1;
    } else {                           /* TAPE: slowing to a stop over a beat */
        float rate = 1.0f - v->el / (tick_len * (float)PS_TPB);
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
    fx.spin_rate = fm_exp2f(3.0f * fm_clampf(fx.spin_t / two, 0.0f, 1.0f)) / (tick_len * (float)PS_TPB);
}

static void svf(float st[2], float x, float f, float q)   /* Chamberlin: st[0] low, st[1] band */
{
    float hi = x - st[0] - q * st[1];
    st[1] += f * hi;
    st[0] += f * st[1];
}

/* the effects over the mix, a frame: l and r in place; what goes to the echo returned (THROW, WASH); n a noise */
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
    /* the filters: LOW down to 250 Hz, HIGH up to 1.6 kHz, BAND a telephone, FLANGE */
    fx.lo_g += ((fx.held >> FX_LOW & 1u ? 1.0f : 0.0f) - fx.lo_g) * (fx.held >> FX_LOW & 1u ? 0.00006f : 0.0003f);
    fx.hi_g += ((fx.held >> FX_HIGH & 1u ? 1.0f : 0.0f) - fx.hi_g) * (fx.held >> FX_HIGH & 1u ? 0.00006f : 0.0003f);
    fx.band_g += ((fx.held >> FX_BAND & 1u ? 1.0f : 0.0f) - fx.band_g) * 0.002f;
    fx.fl_g += ((fx.held >> FX_FLANGE & 1u ? 1.0f : 0.0f) - fx.fl_g) * 0.002f;
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
    /* THROW and WASH: what is above the kick and the bass, into the echo */
    if (fx.held >> FX_THROW & 1u)
        send += 0.6f * (upper_l + upper_r);
    if (fx.held >> FX_WASH & 1u)
        send += 0.5f * (upper_l + upper_r);
    /* RISER: a band of noise, rising over two bars while held */
    fx.riser_g += ((fx.held >> FX_RISER & 1u ? 1.0f : 0.0f) - fx.riser_g) * 0.003f;
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
    /* SPIN: left and right in turn, from a beat's quarter-turn to a 32nd's over two bars */
    fx.spin_g += ((fx.held >> FX_SPIN & 1u ? 1.0f : 0.0f) - fx.spin_g) * 0.003f;
    if (fx.spin_g > 0.0005f) {
        float p = fm_clampf(fx.spin_t / (tick_len * (float)(8u * PS_TPB)), 0.0f, 1.0f), rate, c;
        fx.spin_t += 1.0f;
        rate = fx.spin_rate;           /* turns a frame: a beat's turn, then 8 a beat */
        (void)p;
        if ((fx.spin_ph += rate) >= 1.0f)
            fx.spin_ph -= 1.0f;
        c = fm_sin_turns(fx.spin_ph) * fx.spin_g;
        dl *= 1.0f - 0.9f * (c > 0.0f ? c : 0.0f);
        dr *= 1.0f + 0.9f * (c < 0.0f ? c : 0.0f);
    }
    *l = dl;
    *r = dr;
    return send;
}
