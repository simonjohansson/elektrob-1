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

#endif
