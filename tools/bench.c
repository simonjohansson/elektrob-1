/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The machine's DSP as ob1_render runs it, for the ColdFire: tools/bench.py runs bench() in Unicorn and
 * counts its instructions a block (`mise run bench`). The voice's pitch, knob reads and table lookups in
 * ob1.c are left out: a few dozen instructions a voice. */
#include "dsp.h"

static struct dsp_osc o1[8], o2[8];
static struct dsp_hpf hpf[8];
static uint32_t noise[8];
static int32_t out[8 * 32];

/* nblocks blocks of `voices` voices in one setting:
 * 0  the worst case: both VCOs (pulse type and saw type), both subs high, SYNC and XMOD, noise high
 * 1  the default sound: VCO1, a saw, FUND high
 * 2  both VCOs and their subs high, no sync or cross mod, no noise */
void bench(int nblocks, int voices, int setting)
{
    struct dsp_shape pulse, saw;
    int32_t acc[32], f1[32], s1[32], f2[32], s2[32];
    int b, t, i;
    dsp_shape_pulse(&pulse, 0x30000000u);
    dsp_shape_saw(&saw, 0);
    for (b = 0; b < nblocks; b++)
        for (t = 0; t < voices; t++) {
            uint32_t inc1 = 23410327u + 977u * t, inc2 = 35115490u - 1013u * t;   /* about C4 and G4 */
            if (!noise[t])
                noise[t] = t + 1;
            for (i = 0; i < 32; i++)
                acc[i] = 0;
            if (setting == 0)
                dsp_pair(&o1[t], &o2[t], inc1, inc2, &pulse, &saw, DSP_SYNC | DSP_XMOD, f1, s1, f2, s2, 32);
            else {
                dsp_osc_block(&o1[t], inc1, &saw, f1, s1, 32);
                if (setting == 2)
                    dsp_osc_block(&o2[t], inc2, &saw, f2, s2, 32);
            }
            dsp_mix_add(acc, f1, DSP_MIX_HIGH, 32);
            if (setting != 1) {
                dsp_mix_add(acc, s1, DSP_MIX_HIGH, 32);
                dsp_mix_add(acc, f2, DSP_MIX_HIGH, 32);
                dsp_mix_add(acc, s2, DSP_MIX_HIGH, 32);
            }
            if (setting == 0) {
                dsp_noise(&noise[t], f1, 32);
                dsp_mix_add(acc, f1, DSP_MIX_NOISE_HIGH, 32);
            }
            dsp_mix_out(&hpf[t], acc, 1 << 14, &out[32 * t], 32);
        }
}
