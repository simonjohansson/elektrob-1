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
    int32_t sub;            /* the sub-octave's flip-flop: 32767 or -32768 */
    int32_t sub_prev;       /* its sample before */
};

/* The highest step a waveshape takes: 12 kHz. It keeps the band-limiting's products inside 32 bits. */
#define DSP_INC_MAX (1u << 30)

/* Both waveshapes also render the oscillator's sub-octave (the OB-1's OCT, a 4013 flip-flop clocked by the
 * VCO's reset) into sub: a square an octave down, toggling at each reset, so it follows the ramp's
 * resets, a sync's included. Free-running too: it keeps its state between notes. */

/* Saw type (the OB-1's WAVE in "Saw Type"): g, Q15 from -32768 to 32768, scales the ramp's lower half
 * against its upper: out = max(s, 0) + g min(s, 0). -1 is a triangle (|s|), +1 the saw. Its mean,
 * (1 - g) / 4, is taken off, as the mixer's AC coupling would. n samples into out and sub, one late. */
void dsp_saw_type(struct dsp_osc *o, uint32_t inc, int32_t g, int32_t *out, int32_t *sub, int n);

/* Pulse type (the OB-1's WAVE in "Pulse Type", U11 comparing the ramp with a threshold): +1 while the
 * ramp is below `width` (a fraction of the cycle, 2^31 the square), -1 after. Its mean is taken off. n
 * samples into out, one sample late. */
void dsp_pulse_type(struct dsp_osc *o, uint32_t inc, uint32_t width, int32_t *out, int32_t *sub, int n);

/* ---- the mixer ----------------------------------------------------------------------------------------
 * The OB-1 mixes VCO1, VCO1 OCT, VCO2, VCO2 OCT and NOISE, each switched off, low or high (U13-U15, 4016s
 * into 47K for high and 100K for low; noise 100K and 220K), each AC-coupled at about 11 Hz (47K, 0.1 uF,
 * 100K). Gains against an oscillator at high: */
#define DSP_MIX_HIGH        32768           /* 47K */
#define DSP_MIX_LOW         15420           /* 100K: 47 / 100 */
#define DSP_MIX_NOISE_HIGH  15420           /* 100K */
#define DSP_MIX_NOISE_LOW   7010            /* 220K: 47 / 220 */

/* acc[i] += src[i] * gain (Q15). A gain of 0 skips the source. */
void dsp_mix_add(int32_t *acc, const int32_t *src, int32_t gain, int n);

struct dsp_hpf {
    int32_t lp;             /* the low-pass the high-pass takes off, Q8 */
};

/* The mixed sum out: the 11 Hz high-pass (one for the voice, where the OB-1 has one a source: it only
 * differs at DC), then a quarter, so two oscillators and their subs at high fit; beyond that it clips at
 * full scale, Q31. A single oscillator at high peaks at -12 dB. gain is Q14 (the track's level). */
void dsp_mix_out(struct dsp_hpf *h, const int32_t *acc, int32_t gain, int32_t *out, int n);

#endif
