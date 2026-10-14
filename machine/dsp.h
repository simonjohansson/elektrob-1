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

/* A waveshape, set once a block. The OB-1's two: saw type (WAVE in "Saw Type"): g, Q15 from -32768 to 32768,
 * scales the ramp's lower half against its upper, out = max(s, 0) + g min(s, 0), -1 a triangle (|s|), +1 the
 * saw; pulse type (WAVE in "Pulse Type", U11 comparing the ramp with a threshold): +1 while the ramp is below
 * `width` (a fraction of the cycle, 2^31 the square), -1 after. Each has its mean taken off, as the mixer's
 * AC coupling would. */
struct dsp_shape {
    int32_t pulse;          /* 0 saw type, 1 pulse type */
    int32_t g;              /* saw type */
    uint32_t width;         /* pulse type */
    uint32_t mid;           /* where its second corner or edge is: 2^31 (saw type, s = 0) or width */
    int32_t dc;             /* its mean */
};
void dsp_shape_saw(struct dsp_shape *sh, int32_t g);
void dsp_shape_pulse(struct dsp_shape *sh, uint32_t width);

/* One oscillator a block: n samples of the waveshape into out and of the sub-octave into sub (the OB-1's OCT,
 * a 4013 flip-flop clocked by the VCO's reset: a square an octave down, toggling at each reset, a sync's
 * included, and free-running too), both one sample late. */
void dsp_osc_block(struct dsp_osc *o, uint32_t inc, const struct dsp_shape *sh, int32_t *out, int32_t *sub,
                   int n);

/* The same, a shape at a time. */
void dsp_saw_type(struct dsp_osc *o, uint32_t inc, int32_t g, int32_t *out, int32_t *sub, int n);
void dsp_pulse_type(struct dsp_osc *o, uint32_t inc, uint32_t width, int32_t *out, int32_t *sub, int n);

/* ---- the two VCOs together --------------------------------------------------------------------------
 * SYNC: VCO1's reset pulse resets VCO2 (R120, Q21; SW17). XMOD: VCO2's ramp moves VCO1's pitch through its
 * exponential converter (R4, R5 into U4; SW18), so by DSP_XMOD_OCT octaves each way at the ramp's ends.
 * DSP_XMOD_OCT from sheet 1342 (#22): the saw reaches U4 through 94K against the key CV's 100K, so 1.06
 * octaves a volt, and R100 centres OSC 2 SAW at about +-4.8 V. shortcut: the saw's bottom end is inferred
 * from that centring (the reset comparator's reference is illegible); a clearer scan could refine it. */
#define DSP_SYNC        1
#define DSP_XMOD        2
#define DSP_XMOD_OCT    5

/* Both VCOs a block, sample by sample, with mod (DSP_SYNC | DSP_XMOD): fund and sub of each into f1, s1, f2,
 * s2, one sample late. */
void dsp_pair(struct dsp_osc *o1, struct dsp_osc *o2, uint32_t inc1, uint32_t inc2, const struct dsp_shape *sh1,
              const struct dsp_shape *sh2, int mod, int32_t *f1, int32_t *s1, int32_t *f2, int32_t *s2, int n);

/* The cross mod's pitch factor on v: (v 2^(DSP_XMOD_OCT x / 32768)) << sh (>> -sh if negative), x from
 * -32768 to 32767, v < 2^16, saturating at max. */
uint32_t dsp_xmod_scale(uint32_t v, int32_t x, int32_t sh, uint32_t max);

/* ---- the mixer ----------------------------------------------------------------------------------------
 * The OB-1 mixes VCO1, VCO1 OCT, VCO2, VCO2 OCT and NOISE, each switched off, low or high (U13-U15, 4016s
 * into 47K for high and 100K for low; noise 100K and 220K), each AC-coupled at about 11 Hz (47K, 0.1 uF,
 * 100K). Gains against an oscillator at high: */
#define DSP_MIX_HIGH        4096            /* 47K, Q12 */
#define DSP_MIX_LOW         1925            /* 100K: 47 / 100 */
#define DSP_MIX_NOISE_HIGH  1925            /* 100K */
#define DSP_MIX_NOISE_LOW   875             /* 220K: 47 / 220 */

/* The noise source (on the OB-1 a transistor junction's noise, from the performance board): white, from a
 * 32-bit xorshift whose state must not be 0. n samples, -32768 to 32767. */
void dsp_noise(uint32_t *state, int32_t *out, int n);

struct dsp_hpf {
    int32_t lp;             /* the low-pass the high-pass takes off, Q8 */
};

/* A voice's output, in one pass: src[0-3] (VCO1, VCO1 OCT, VCO2, VCO2 OCT) at gain[0-3] (Q12: a source at
 * gain 0 may point at anything readable), and noise from *noise at noise_gain (0: none), summed; the 11 Hz
 * high-pass (one for the voice, where the OB-1 has one a source: it only differs at DC); a quarter, so two
 * oscillators and their subs at high fit, and beyond that clipped at full scale, Q31. A single oscillator
 * at high peaks at -12 dB. */
void dsp_voice_out(struct dsp_hpf *h, const int32_t *const src[4], const int32_t gain[4], uint32_t *noise,
                   int32_t noise_gain, int32_t *out, int n);

#endif
