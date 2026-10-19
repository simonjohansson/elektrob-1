/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "dsp.h"

/* The per-sample helpers are inlined, and the loops compiled once per waveshape (`pulse` a constant), so the
 * hot loop has no calls and no branch on the shape. */
#define HOT static inline __attribute__((always_inline))

void ob1_dsp_vco_set(struct dsp_vco *v, uint32_t inc)
{
    uint32_t i = inc, k = 0;
    if (i >= 1u << 16) {
        k = 16 - __builtin_clz(i);
        i >>= k;
    }
    v->inc = inc;
    v->shift = k;
    v->rinc = i ? (1u << 30) / i : 0;
}

/* The residuals of a kernel two samples wide, x the time from the event in samples:
 * jump:  before (x + 1)^2 / 2, after -(1 - x)^2 / 2;  slope: before (x + 1)^3 / 6, after (1 - x)^3 / 6.
 * The previous sample is 1 - t before the event (x + 1 = t), the current one t after. |h| <= 65536 and
 * |m| <= 32768 (DSP_INC_MAX); the positive full-scale jump is handled before multiplying. */
HOT void blep(int32_t *prev, int32_t *cur, int32_t t, int32_t h, int32_t m)
{
    int32_t a = t >> 1, b = 32768 - a;          /* t and 1 - t, Q15 */
    int32_t a2 = (a * a) >> 15, b2 = (b * b) >> 15;
    if (h == 65536) {
        *prev += a2;
        *cur -= b2;
    } else {
        *prev += (h * a2) >> 16;
        *cur -= (h * b2) >> 16;
    }
    if (m) {
        int32_t a3 = (a2 * a) >> 15, b3 = (b2 * b) >> 15;
        *prev += (((m * a3) >> 15) * 10923) >> 16;     /* / 6 */
        *cur += (((m * b3) >> 15) * 10923) >> 16;
    }
}

void ob1_dsp_blep(int32_t *prev, int32_t *cur, int32_t t, int32_t h, int32_t m)
{
    blep(prev, cur, t, h, m);
}

void ob1_dsp_shape_saw(struct dsp_shape *sh, int32_t g)
{
    sh->pulse = 0;
    sh->g = g;
    sh->width = 0;
    sh->mid = 0x80000000u;
    sh->dc = (g - 32768) >> 2;                  /* the mean, (g - 1) / 4 */
}

void ob1_dsp_shape_pulse(struct dsp_shape *sh, uint32_t width)
{
    sh->pulse = 1;
    sh->g = 0;
    sh->width = width;
    sh->mid = width;
    sh->dc = (int32_t)(width >> 16) - 32768;    /* the mean, 2 width - 1 */
}

/* The shape's value at a phase, its mean taken off. */
HOT int32_t value(const struct dsp_shape *sh, int pulse, uint32_t ph)
{
    int32_t s;
    if (pulse == 1)
        return (ph < sh->width ? 32767 : -32768) - sh->dc;
    s = dsp_saw(ph);
    if (pulse == 2)
        return s;
    return (s < 0 ? s : (sh->g * s) >> 15) - sh->dc;
}

/* Its slope a sample at a phase, the ramp rising ds a sample (Q15). */
HOT int32_t slope(const struct dsp_shape *sh, int pulse, uint32_t ph, int32_t ds)
{
    if (pulse == 1)
        return 0;
    if (pulse == 2)
        return ds;
    return ph < 0x80000000u ? ds : (sh->g * ds) >> 15;
}

/* Mode 0 is the shaped ramp, 1 pulse, and 2 the full-saw fast path. */
/* Its jump and change of slope at the reset (0) and at mid. */
HOT void at_reset(const struct dsp_shape *sh, int pulse, int32_t ds, int32_t *h, int32_t *m)
{
    *h = pulse == 1 ? 65536 : pulse == 2 ? -65536 : -(32768 + sh->g);
    *m = pulse ? 0 : ((32768 - sh->g) * ds) >> 15;
}
HOT void at_mid(const struct dsp_shape *sh, int pulse, int32_t ds, int32_t *h, int32_t *m)
{
    *h = pulse == 1 ? -65536 : 0;           /* down; or the saw type's kink */
    *m = pulse ? 0 : -(((32768 - sh->g) * ds) >> 15);
}

/* The sub-octave's flip-flop toggles t into the sample: its jump's polyBLEP, onto ys, its sample. */
HOT void toggle(struct dsp_osc *o, int32_t *sprev, int32_t *ys, int32_t t)
{
    int32_t was = o->sub;
    o->sub = was > 0 ? -32768 : 32767;
    *ys += o->sub - was;
    blep(sprev, ys, t, o->sub - was, 0);
}

/* One sample of one oscillator: its ramp advanced by o->vco.inc, or reset `sync` (Q16) before the sample's
 * end if sync >= 0. Writes its waveshape's and sub-octave's samples, one late, and returns where its ramp
 * reset in the sample, or -1. */
HOT int32_t tick(struct dsp_osc *o, const struct dsp_shape *sh, int pulse, int32_t sync, int32_t *out,
                int32_t *sub)
{
    struct dsp_vco *v = &o->vco;
    uint32_t old = v->phase, at, np, d;
    int32_t ds = (int32_t)(v->inc >> 16), y, ys = o->sub, h, m, reset = -1, t, merged = 0;
    v->phase = old + v->inc;
    if (sync < 0) {
        y = value(sh, pulse, v->phase);
        if ((reset = dsp_vco_crossed(v, 0)) >= 0) {
            at_reset(sh, pulse, ds, &h, &m);
            blep(&o->prev, &y, reset, h, m);
            toggle(o, &o->sub_prev, &ys, reset);
        }
        if (pulse != 2 && (t = dsp_vco_crossed(v, sh->mid)) >= 0) {
            at_mid(sh, pulse, ds, &h, &m);
            blep(&o->prev, &y, t, h, m);
        }
    } else {
        /* the ramp since the sync, sync x inc: the low half as well, or it lags VCO1 by up to 1 % of a step */
        np = (v->inc >> 16) * (uint32_t)sync + (((v->inc & 0xffff) * (uint32_t)sync) >> 16);
        at = v->phase - np;                     /* where it was when the sync came */
        y = value(sh, pulse, np);
        /* its own reset within 1/1024 of a sample before the sync is the sync's, as on the OB-1 one reset
         * pulse: counted apart, rounding picks the cycles it lands on, and a whole-multiple FRQ2 gets a
         * rhythm. In this sample, the sync takes it from the top; in the one before, it has already reset
         * and toggled the sub-octave, so the sync does not toggle it again. */
        if (at < v->inc >> 10) {
            if (at < at - old)
                at = 0xffffffffu;
            else
                merged = 1;
        }
        /* its own reset and mid, if it passed them earlier in the sample, before the sync: t is the sync's
         * plus how long before it, (at - point) / inc */
        if ((d = at) < at - old) {
            reset = sync + (int32_t)((((d >> v->shift) * v->rinc) >> 14));
            if (reset < 65536) {
                at_reset(sh, pulse, ds, &h, &m);
                blep(&o->prev, &y, reset, h, m);
                toggle(o, &o->sub_prev, &ys, reset);
            }
        }
        if ((d = at - sh->mid) < at - old && (t = sync + (int32_t)(((d >> v->shift) * v->rinc) >> 14)) < 65536) {
            at_mid(sh, pulse, ds, &h, &m);
            blep(&o->prev, &y, t, h, m);
        }
        /* the sync: from where it was to the start of the ramp. shortcut: an edge the restarted ramp passes
         * in the same sample (a very narrow pulse near 12 kHz) is not band-limited. */
        blep(&o->prev, &y, sync, value(sh, pulse, 0) - value(sh, pulse, at), slope(sh, pulse, 0, ds) - slope(sh, pulse, at, ds));
        if (!merged)
            toggle(o, &o->sub_prev, &ys, sync);
        v->phase = np;
        reset = sync;
    }
    *out = o->prev;
    o->prev = y;
    *sub = o->sub_prev;
    o->sub_prev = ys;
    return reset;
}

void ob1_dsp_osc_block(struct dsp_osc *o, uint32_t inc, const struct dsp_shape *sh, int32_t *out, int32_t *sub,
                   int n)
{
    struct dsp_osc a;                           /* a local copy: out and sub cannot alias it, so it stays in
                                                   registers through the loop */
    struct dsp_shape h = *sh;
    int i;
    ob1_dsp_vco_set(&o->vco, inc > DSP_INC_MAX ? DSP_INC_MAX : inc);
    a = *o;
    if (h.pulse)
        for (i = 0; i < n; i++)
            tick(&a, &h, 1, -1, &out[i], &sub[i]);
    else if (h.g == 32768)                       /* full saw needs neither half scaling nor a middle corner */
        for (i = 0; i < n; i++)
            tick(&a, &h, 2, -1, &out[i], &sub[i]);
    else
        for (i = 0; i < n; i++)
            tick(&a, &h, 0, -1, &out[i], &sub[i]);
    *o = a;
}

void ob1_dsp_saw_type(struct dsp_osc *o, uint32_t inc, int32_t g, int32_t *out, int32_t *sub, int n)
{
    struct dsp_shape sh;
    ob1_dsp_shape_saw(&sh, g);
    ob1_dsp_osc_block(o, inc, &sh, out, sub, n);
}

void ob1_dsp_pulse_type(struct dsp_osc *o, uint32_t inc, uint32_t width, int32_t *out, int32_t *sub, int n)
{
    struct dsp_shape sh;
    ob1_dsp_shape_pulse(&sh, width);
    ob1_dsp_osc_block(o, inc, &sh, out, sub, n);
}

/* 2^((k - 32) / 32), Q14, k 0-64: round(16384 * 2 ** ((k - 32) / 32)). The cross mod uses k 32-64. */
static const uint16_t exp2_tab[65] = {
    8192, 8371, 8555, 8742, 8933, 9129, 9329, 9533,
    9742, 9955, 10173, 10396, 10624, 10856, 11094, 11337,
    11585, 11839, 12098, 12363, 12634, 12910, 13193, 13482,
    13777, 14079, 14387, 14702, 15024, 15353, 15689, 16033,
    16384, 16743, 17109, 17484, 17867, 18258, 18658, 19066,
    19484, 19911, 20347, 20792, 21247, 21713, 22188, 22674,
    23170, 23678, 24196, 24726, 25268, 25821, 26386, 26964,
    27554, 28158, 28774, 29405, 30048, 30706, 31379, 32066,
    32768,
};

/* 2^(f / 65536), Q14 (16384-32768), f 0-65535. */
HOT uint32_t exp2_frac(uint32_t f)
{
    uint32_t k = 32 + (f >> 11);
    return exp2_tab[k] + (((exp2_tab[k + 1] - exp2_tab[k]) * (f & 2047)) >> 11);
}

/* (v << sh) 2^(u / 65536 - DSP_XMOD_OCT), u the octaves Q16, saturating at max (v < 2^16). */
HOT uint32_t xmod_scale(uint32_t v, uint32_t u, int32_t sh, uint32_t max)
{
    uint32_t p = v * exp2_frac(u & 0xffff);     /* < 2^31 */
    int32_t s = (int32_t)(u >> 16) - DSP_XMOD_OCT + sh;
    if (s < 0)
        /* In the oscillator path p <= 2^31, so any right shift fits the step limit. */
        return max == DSP_INC_MAX && sh == 0 ? p >> -s : ((p >> -s) > max ? max : p >> -s);
    return p > (max >> s) ? max : p << s;
}

/* Fractional depth in Q16 octaves, biased by the lookup's integer ceiling. */
#define XMOD_U(x)   ((((uint32_t)((x) + 32768) * DSP_XMOD_DEPTH_Q6) >> 5) + \
                     (DSP_XMOD_OCT * 65536u - DSP_XMOD_DEPTH_Q6 * 1024u))

uint32_t ob1_dsp_xmod_scale(uint32_t v, int32_t x, int32_t sh, uint32_t max)
{
    return xmod_scale(v, XMOD_U(x), sh, max);
}

/* ob1_dsp_pair's loop, compiled once for each pair of waveshapes. */
HOT void pair(struct dsp_osc *o1, struct dsp_osc *o2, const struct dsp_shape *sh1, const struct dsp_shape *sh2,
              int p1, int p2, int mod, int32_t *f1, int32_t *s1, int32_t *f2, int32_t *s2, int n)
{
    struct dsp_osc a = *o1, b = *o2;            /* local copies, as in ob1_dsp_osc_block */
    struct dsp_shape h1 = *sh1, h2 = *sh2;
    uint32_t base = a.vco.inc >> 14, rbase = a.vco.rinc;
    int32_t reset;
    int i;
    for (i = 0; i < n; i++) {
        if (mod & DSP_XMOD) {                   /* VCO1's step, times 2^x */
            uint32_t u = (((b.vco.phase >> 16) * DSP_XMOD_DEPTH_Q6) >> 5) +
                         (DSP_XMOD_OCT * 65536u - DSP_XMOD_DEPTH_Q6 * 1024u), np, r;
            a.vco.inc = xmod_scale(base, u, 0, DSP_INC_MAX);
            np = a.vco.phase + a.vco.inc;
            /* its reciprocal, times 2^-x, only for a sample whose ramp passes 0 or the corner (tick's
             * dsp_vco_crossed): once or twice a cycle, not every sample */
            if (np < a.vco.inc || (p1 != 2 && np - h1.mid < a.vco.inc)) {
                r = 2 * DSP_XMOD_OCT * 65536u - 1 - u;  /* complementary exponent, rounded down */
                a.vco.rinc = a.vco.inc == DSP_INC_MAX ? 1u << a.vco.shift  /* 2^30 / (DSP_INC_MAX >> shift) */
                                                      : (rbase * exp2_frac(r & 0xffff)) >> (14 + DSP_XMOD_OCT - (r >> 16));
            }
        }
        reset = tick(&a, &h1, p1, -1, &f1[i], &s1[i]);
        tick(&b, &h2, p2, (mod & DSP_SYNC) ? reset : -1, &f2[i], &s2[i]);
    }
    a.vco.inc = base << 14;                     /* the block's own step back, for the next block's start */
    a.vco.rinc = rbase;
    *o1 = a;
    *o2 = b;
}

void ob1_dsp_pair(struct dsp_osc *o1, struct dsp_osc *o2, uint32_t inc1, uint32_t inc2, const struct dsp_shape *sh1,
              const struct dsp_shape *sh2, int mod, int32_t *f1, int32_t *s1, int32_t *f2, int32_t *s2, int n)
{
    ob1_dsp_vco_set(&o1->vco, inc1 > DSP_INC_MAX ? DSP_INC_MAX : inc1);
    ob1_dsp_vco_set(&o2->vco, inc2 > DSP_INC_MAX ? DSP_INC_MAX : inc2);
    if (!sh1->pulse && sh1->g == 32768) {
        if (sh2->pulse)
            pair(o1, o2, sh1, sh2, 2, 1, mod, f1, s1, f2, s2, n);
        else if (sh2->g == 32768)
            pair(o1, o2, sh1, sh2, 2, 2, mod, f1, s1, f2, s2, n);
        else
            pair(o1, o2, sh1, sh2, 2, 0, mod, f1, s1, f2, s2, n);
    } else if (sh1->pulse && sh2->pulse)
        pair(o1, o2, sh1, sh2, 1, 1, mod, f1, s1, f2, s2, n);
    else if (sh1->pulse)
        pair(o1, o2, sh1, sh2, 1, 0, mod, f1, s1, f2, s2, n);
    else if (sh2->pulse)
        pair(o1, o2, sh1, sh2, 0, 1, mod, f1, s1, f2, s2, n);
    else
        pair(o1, o2, sh1, sh2, 0, 0, mod, f1, s1, f2, s2, n);
}

HOT uint32_t noise_next(uint32_t x)
{
    return x * 1664525u + 1013904223u;
}

void ob1_dsp_noise(uint32_t *state, int32_t *out, int n)
{
    uint32_t x = *state;
    int i;
    for (i = 0; i < n; i++)
        out[i] = (int32_t)(x = noise_next(x)) >> 16;
    *state = x;
}

/* ob1_dsp_voice_out's loop, compiled with and without the noise, and for VCO1's two sources alone (four: false)
 * or all four: fewer live values, fewer spilled to the stack. */
HOT void voice_out(struct dsp_hpf *h, const int32_t *const src[4], const int32_t gain[4], uint32_t *noise,
                   int32_t ng, int32_t *out, int n, int with_noise, int four)
{
    const int32_t *a = src[0], *b = src[1], *c = src[2], *d = src[3];
    int32_t ga = gain[0], gb = gain[1], gc = gain[2], gd = gain[3], lp = h->lp, x, i;
    uint32_t z = *noise;
    for (i = 0; i < n; i++) {
        x = a[i] * ga + b[i] * gb;                              /* |src| < 2^16, gain <= 2^12: < 2^30 */
        if (four)
            x += c[i] * gc + d[i] * gd;
        if (with_noise) {
            z = noise_next(z);
            int32_t raw = (int32_t)z >> 16;
            x += raw * ng;
        }
        x >>= 14;                               /* Q12 to Q15, and a quarter */
        lp += (((x * 256) - lp) * 47) >> 15;     /* the low-pass the high-pass takes off: 2 pi 11 / 48000 */
        x -= lp >> 8;
        out[i] = x > 32767 ? 0x7fffffff : x < -32768 ? (int32_t)0x80000000 : x * 65536;
    }
    h->lp = lp;
    *noise = z;
}

void ob1_dsp_voice_out(struct dsp_hpf *h, const int32_t *const src[4], const int32_t gain[4], uint32_t *noise,
                   int32_t noise_gain, int32_t *out, int n)
{
    int four = gain[2] || gain[3];
    if (noise_gain && four)
        voice_out(h, src, gain, noise, noise_gain, out, n, 1, 1);
    else if (noise_gain)
        voice_out(h, src, gain, noise, noise_gain, out, n, 1, 0);
    else if (four)
        voice_out(h, src, gain, noise, 0, out, n, 0, 1);
    else
        voice_out(h, src, gain, noise, 0, out, n, 0, 0);
}
