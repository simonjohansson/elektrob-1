/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The machine's sound, with no Digitakt dependencies: built for the ColdFire by the SDK and for the host
 * by `mise run test` and `mise run render`. */
#ifndef DSP_H
#define DSP_H
#include <stdint.h>

/* sin(2 pi ph / 2^32), Q15. */
int32_t dsp_sine(uint32_t ph);

/* A sine, folded back into range after a gain of 1 + shape / 16; shape 0-127. */
int32_t dsp_fold(int32_t y, int32_t shape);

/* n samples of the folded sine into out (Q31), advancing *phase by inc a sample; gain Q14. */
void dsp_render(uint32_t *phase, uint32_t inc, int32_t shape, int32_t gain, int32_t *out, int n);

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

/* One sample: advance the ramp. -> -1 if it did not reset during this sample, or else how far past the
 * reset the sample ends, as a fraction of a sample, Q16 (0-65535): the t a polyBLEP takes. */
static inline int32_t dsp_vco_step(struct dsp_vco *v)
{
    uint32_t p = v->phase + v->inc, t;
    v->phase = p;
    if (p >= v->inc)
        return -1;
    t = ((p >> v->shift) * v->rinc) >> 14;    /* p / inc, Q16: p < inc, so the product < 2^30 */
    return t > 65535 ? 65535 : (int32_t)t;
}

/* The ramp as a rising saw, Q15: -32768 just after the reset, 32767 just before. */
static inline int32_t dsp_saw(uint32_t phase)
{
    return (int32_t)(phase - 0x80000000u) >> 16;
}

#endif
