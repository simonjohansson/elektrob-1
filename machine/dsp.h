/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The machine's sound, with no Digitakt dependencies: built for the ColdFire by the SDK and for the host
 * by `mise run test` and `mise run render`. Samples are Q15 (1.0 = 32768) unless a comment says not. */
#ifndef DSP_H
#define DSP_H
#include <stdint.h>

/* ---- the VCO core: a free-running rising ramp, as the OB-1's (C2 charged, reset by Q7) ---------------
 * The phase is the ramp, a full cycle 2^32. Every waveshape, the sync and the sub-octave derive from it,
 * and they need to know where inside a sample the reset fell: dsp_vco_step returns that. */
struct dsp_vco {
    uint32_t phase;         /* the ramp: 0 just after a reset */
    uint32_t inc;           /* a sample's step: 2^32 * f / 48000 */
    uint32_t rinc;          /* 2^30 / (inc >> shift), for the reset's position without a divide */
    uint32_t shift;         /* inc >> shift fits 16 bits */
};

/* Once a block: the step for this block's pitch. The only divide. */
void dsp_vco_set(struct dsp_vco *v, uint32_t inc);

/* After a step: if the ramp passed `at` during this sample, how far past it the sample ends, as a
 * fraction of a sample, Q16 (0-65535): the t a polyBLEP takes. Else -1. */
static inline int32_t dsp_vco_crossed(const struct dsp_vco *v, uint32_t at)
{
    uint32_t d = v->phase - at, t;
    if (d >= v->inc)
        return -1;
    t = ((d >> v->shift) * v->rinc) >> 14;    /* d / inc, Q16: d < inc, so the product < 2^30 */
    return t > 65535 ? 65535 : (int32_t)t;
}

/* One sample: advance the ramp. -> where in the sample it reset (dsp_vco_crossed at 0), or -1. */
static inline int32_t dsp_vco_step(struct dsp_vco *v)
{
    v->phase += v->inc;
    return dsp_vco_crossed(v, 0);
}

/* The ramp as a rising saw: -32768 just after the reset, 32767 just before. */
static inline int32_t dsp_saw(uint32_t phase)
{
    return (int32_t)(phase - 0x80000000u) >> 16;
}

/* ---- band-limiting ----------------------------------------------------------------------------------
 * A jump of h, or a change of slope of m a sample, t (Q16) before the current sample: the 2-point
 * polyBLEP and polyBLAMP residuals, added to the previous sample (not yet output) and the current one. */
void dsp_blep(int32_t *prev, int32_t *cur, int32_t t, int32_t h, int32_t m);

/* ---- one oscillator ---------------------------------------------------------------------------------- */
struct dsp_osc {
    struct dsp_vco vco;
    int32_t prev;           /* the sample before, held back a sample for its polyBLEP share */
};

/* The highest step a waveshape takes: 12 kHz. It keeps the band-limiting's products inside 32 bits. */
#define DSP_INC_MAX (1u << 30)

/* Saw type (the OB-1's WAVE in "Saw Type"): g, Q15 from -32768 to 32768, scales the ramp's lower half
 * against its upper: out = max(s, 0) + g min(s, 0). -1 is a triangle (|s|), +1 the saw. Its mean,
 * (1 - g) / 4, is taken off, as the mixer's AC coupling would. n samples into out, one sample late. */
void dsp_saw_type(struct dsp_osc *o, uint32_t inc, int32_t g, int32_t *out, int n);

#endif
