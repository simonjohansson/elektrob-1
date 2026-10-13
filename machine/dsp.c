/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "dsp.h"

void dsp_vco_set(struct dsp_vco *v, uint32_t inc)
{
    uint32_t i = inc, k = 0;
    while (i >= 1u << 16) {                     /* at most 16 shifts, once a block */
        i >>= 1;
        k++;
    }
    v->inc = inc;
    v->shift = k;
    v->rinc = i ? (1u << 30) / i : 0;
}

/* The residuals of a kernel two samples wide, x the time from the event in samples:
 * jump:  before (x + 1)^2 / 2, after -(1 - x)^2 / 2;  slope: before (x + 1)^3 / 6, after (1 - x)^3 / 6.
 * The previous sample is 1 - t before the event (x + 1 = t), the current one t after. |h| <= 65536 and
 * |m| <= 32768 (DSP_INC_MAX) keep every product inside 32 bits. */
void dsp_blep(int32_t *prev, int32_t *cur, int32_t t, int32_t h, int32_t m)
{
    int32_t a = t >> 1, b = 32768 - a;          /* t and 1 - t, Q15 */
    int32_t a2 = (a * a) >> 15, b2 = (b * b) >> 15;
    *prev += (h * a2) >> 16;
    *cur -= (h * b2) >> 16;
    if (m) {
        int32_t a3 = (a2 * a) >> 15, b3 = (b2 * b) >> 15;
        *prev += (((m * a3) >> 15) * 10923) >> 16;     /* / 6 */
        *cur += (((m * b3) >> 15) * 10923) >> 16;
    }
}

void dsp_shape_saw(struct dsp_shape *sh, int32_t g)
{
    sh->pulse = 0;
    sh->g = g;
    sh->width = 0;
    sh->mid = 0x80000000u;
    sh->dc = (32768 - g) >> 2;                  /* the mean, (1 - g) / 4 */
}

void dsp_shape_pulse(struct dsp_shape *sh, uint32_t width)
{
    sh->pulse = 1;
    sh->g = 0;
    sh->width = width;
    sh->mid = width;
    sh->dc = (int32_t)(width >> 16) - 32768;    /* the mean, 2 width - 1 */
}

/* The shape's value at a phase, its mean taken off. */
static inline int32_t value(const struct dsp_shape *sh, uint32_t ph)
{
    int32_t s;
    if (sh->pulse)
        return (ph < sh->width ? 32767 : -32768) - sh->dc;
    s = dsp_saw(ph);
    return (s > 0 ? s : (sh->g * s) >> 15) - sh->dc;
}

/* Its slope a sample at a phase, the ramp rising ds a sample (Q15). */
static inline int32_t slope(const struct dsp_shape *sh, uint32_t ph, int32_t ds)
{
    if (sh->pulse)
        return 0;
    return ph >= 0x80000000u ? ds : (sh->g * ds) >> 15;
}

/* Its jump and change of slope at the reset (0) and at mid. */
static inline void at_reset(const struct dsp_shape *sh, int32_t ds, int32_t *h, int32_t *m)
{
    *h = sh->pulse ? 65536 : -(32768 + sh->g);  /* up; or from 1 to -g */
    *m = sh->pulse ? 0 : -(((32768 - sh->g) * ds) >> 15);
}
static inline void at_mid(const struct dsp_shape *sh, int32_t ds, int32_t *h, int32_t *m)
{
    *h = sh->pulse ? -65536 : 0;                /* down; or the saw type's kink */
    *m = sh->pulse ? 0 : ((32768 - sh->g) * ds) >> 15;
}

/* The sub-octave's flip-flop toggles t into the sample: its jump's polyBLEP, onto ys, its sample. */
static inline void toggle(struct dsp_osc *o, int32_t *sprev, int32_t *ys, int32_t t)
{
    int32_t was = o->sub;
    o->sub = was > 0 ? -32768 : 32767;
    *ys += o->sub - was;
    dsp_blep(sprev, ys, t, o->sub - was, 0);
}

/* One sample of one oscillator: its ramp advanced by o->vco.inc, or reset `sync` (Q16) before the sample's
 * end if sync >= 0. Writes its waveshape's and sub-octave's samples, one late, and returns where its ramp
 * reset in the sample, or -1. */
static inline int32_t tick(struct dsp_osc *o, const struct dsp_shape *sh, int32_t sync, int32_t *out,
                           int32_t *sub)
{
    struct dsp_vco *v = &o->vco;
    uint32_t old = v->phase, at, np, d;
    int32_t ds = (int32_t)(v->inc >> 16), y, ys = o->sub, h, m, reset = -1, t;
    v->phase = old + v->inc;
    if (sync < 0) {
        y = value(sh, v->phase);
        if ((reset = dsp_vco_crossed(v, 0)) >= 0) {
            at_reset(sh, ds, &h, &m);
            dsp_blep(&o->prev, &y, reset, h, m);
            toggle(o, &o->sub_prev, &ys, reset);
        }
        if ((t = dsp_vco_crossed(v, sh->mid)) >= 0) {
            at_mid(sh, ds, &h, &m);
            dsp_blep(&o->prev, &y, t, h, m);
        }
    } else {
        np = (v->inc >> 16) * (uint32_t)sync;  /* the ramp since the sync, about sync x inc */
        at = v->phase - np;                     /* where it was when the sync came */
        y = value(sh, np);
        /* its own reset and mid, if it passed them earlier in the sample, before the sync: t is the sync's
         * plus how long before it, (at - point) / inc */
        if ((d = at) < at - old) {
            reset = sync + (int32_t)((((d >> v->shift) * v->rinc) >> 14));
            if (reset < 65536) {
                at_reset(sh, ds, &h, &m);
                dsp_blep(&o->prev, &y, reset, h, m);
                toggle(o, &o->sub_prev, &ys, reset);
            }
        }
        if ((d = at - sh->mid) < at - old && (t = sync + (int32_t)(((d >> v->shift) * v->rinc) >> 14)) < 65536) {
            at_mid(sh, ds, &h, &m);
            dsp_blep(&o->prev, &y, t, h, m);
        }
        /* the sync: from where it was to the start of the ramp. shortcut: an edge the restarted ramp passes
         * in the same sample (a very narrow pulse near 12 kHz) is not band-limited. */
        dsp_blep(&o->prev, &y, sync, value(sh, 0) - value(sh, at), slope(sh, 0, ds) - slope(sh, at, ds));
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

void dsp_osc_block(struct dsp_osc *o, uint32_t inc, const struct dsp_shape *sh, int32_t *out, int32_t *sub,
                   int n)
{
    int i;
    dsp_vco_set(&o->vco, inc > DSP_INC_MAX ? DSP_INC_MAX : inc);
    for (i = 0; i < n; i++)
        tick(o, sh, -1, &out[i], &sub[i]);
}

void dsp_saw_type(struct dsp_osc *o, uint32_t inc, int32_t g, int32_t *out, int32_t *sub, int n)
{
    struct dsp_shape sh;
    dsp_shape_saw(&sh, g);
    dsp_osc_block(o, inc, &sh, out, sub, n);
}

void dsp_pulse_type(struct dsp_osc *o, uint32_t inc, uint32_t width, int32_t *out, int32_t *sub, int n)
{
    struct dsp_shape sh;
    dsp_shape_pulse(&sh, width);
    dsp_osc_block(o, inc, &sh, out, sub, n);
}

/* 2^((k - 32) / 32), Q14, k 0-64: round(16384 * 2 ** ((k - 32) / 32)). */
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

int32_t dsp_xmod_factor(int32_t x)
{
    uint32_t u = (uint32_t)(x + 32768), k = u >> 10, f = u & 1023;
    return exp2_tab[k] + (((exp2_tab[k + 1] - exp2_tab[k]) * (int32_t)f) >> 10);
}

void dsp_pair(struct dsp_osc *o1, struct dsp_osc *o2, uint32_t inc1, uint32_t inc2, const struct dsp_shape *sh1,
              const struct dsp_shape *sh2, int mod, int32_t *f1, int32_t *s1, int32_t *f2, int32_t *s2, int n)
{
    uint32_t base, rbase;
    int32_t reset;
    int i;
    dsp_vco_set(&o1->vco, inc1 > DSP_INC_MAX ? DSP_INC_MAX : inc1);
    dsp_vco_set(&o2->vco, inc2 > DSP_INC_MAX ? DSP_INC_MAX : inc2);
    base = o1->vco.inc >> 14;
    rbase = o1->vco.rinc;
    for (i = 0; i < n; i++) {
        if (mod & DSP_XMOD) {                   /* VCO1's step and its reciprocal, times 2^x and 2^-x */
            int32_t x = dsp_saw(o2->vco.phase);
            uint32_t inc = base * (uint32_t)dsp_xmod_factor(x);
            o1->vco.inc = inc > DSP_INC_MAX ? DSP_INC_MAX : inc;
            o1->vco.rinc = (rbase * (uint32_t)dsp_xmod_factor(-1 - x)) >> 14;
        }
        reset = tick(o1, sh1, -1, &f1[i], &s1[i]);
        tick(o2, sh2, (mod & DSP_SYNC) ? reset : -1, &f2[i], &s2[i]);
    }
}

void dsp_noise(uint32_t *state, int32_t *out, int n)
{
    uint32_t x = *state;
    int i;
    for (i = 0; i < n; i++) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        out[i] = (int32_t)x >> 16;
    }
    *state = x;
}

void dsp_mix_add(int32_t *acc, const int32_t *src, int32_t gain, int n)
{
    int i;
    if (!gain)
        return;
    for (i = 0; i < n; i++)
        acc[i] += (src[i] * gain) >> 15;        /* |src| < 2^16 and gain <= 2^15 */
}

void dsp_mix_out(struct dsp_hpf *h, const int32_t *acc, int32_t gain, int32_t *out, int n)
{
    int32_t lp = h->lp, x, y, i;
    for (i = 0; i < n; i++) {
        x = acc[i] >> 2;                        /* a quarter: < 2^16 for five sources */
        lp += (((x << 8) - lp) * 47) >> 15;     /* 2 pi 11 / 48000 = 47 / 32768 */
        y = (x - (lp >> 8)) * gain;             /* Q15 x Q14 */
        out[i] = y > 0x1fffffff ? 0x7fffffff : y < -0x20000000 ? (int32_t)0x80000000 : y << 2;
    }
    h->lp = lp;
}
