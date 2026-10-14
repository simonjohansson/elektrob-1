/* SPDX-License-Identifier: GPL-2.0-or-later */
/* One OB-1 voice from its knobs, with no Digitakt dependencies: the machine (machine/ob1.c), the host render
 * (tools/render.c) and the benchmark (tools/bench.c) all run this, so a host render is the machine's sound. */
#ifndef VOICE_H
#define VOICE_H
#include "dsp.h"

/* The SRC page's knobs, as the machine reads them. */
struct ob1_knobs {
    int32_t wav1, wav2;     /* 0-63 saw type, triangle to saw; 64-127 pulse type, narrowest to square */
    int32_t mix1, mix2;     /* 0-8: FUND x OCT, each OFF, -3 dB, ON */
    int32_t mod;            /* 0-3: OFF, SYNC, XMOD, BOTH */
    int32_t nois;           /* 0-2: OFF, -3 dB, ON */
};

struct ob1_voice {
    struct dsp_osc vco1, vco2;  /* free-running, as the OB-1's VCOs: never reset */
    struct dsp_hpf hpf;
    uint32_t noise;             /* the noise's state: never 0 */
};

/* WAV1/WAV2's value (0-127) as a waveshape. */
void ob1_shape(struct dsp_shape *sh, int32_t w);

/* A voice at rest; seed (not 0) sets its noise apart from other voices'. */
void ob1_voice_init(struct ob1_voice *v, uint32_t seed);

/* One block of 32 samples into out (Q31): VCO1 at step inc1, VCO2 at inc2 (the caller adds FRQ2 to the
 * pitch: a table on the unit, pow() on the host). */
void ob1_voice_render(struct ob1_voice *v, const struct ob1_knobs *k, uint32_t inc1, uint32_t inc2, int32_t *out);

#endif
