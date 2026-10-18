/* SPDX-License-Identifier: GPL-2.0-or-later */
/* One OB-1 voice from its knobs, with no device dependencies: the Digitakt wrapper (machines/digitakt/ob1.c),
 * the host render (tools/render.c) and the benchmark (tools/bench.c) all run this. */
#ifndef VOICE_H
#define VOICE_H
#include "dsp.h"

/* Decoded sound-engine controls, independent of a device's knob layout. */
struct ob1_knobs {
    int32_t wav1, wav2;     /* 0-63 saw type, triangle to saw; 64-127 pulse type, narrowest to square */
    int32_t mix1, mix2;     /* 0-8: FUND x OCT, each OFF, -3 dB, ON */
    int32_t mod;            /* 0-3: OFF, SYNC, XMOD, BOTH */
    int32_t nois;           /* 0-2: OFF, -3 dB, ON */
};

struct ob1_voice {
    struct dsp_osc vco1, vco2;  /* phase retained between notes; paused when rendering is skipped */
    struct dsp_hpf hpf;
    uint32_t noise;             /* deterministic per-track noise state */
};

/* WAV1/WAV2's value (0-127) as a waveshape. */
void ob1_shape(struct dsp_shape *sh, int32_t w);

/* A voice at rest; seed (not 0) sets its noise apart from other voices'. */
void ob1_voice_init(struct ob1_voice *v, uint32_t seed);

/* One block of 32 samples into out (Q31): VCO1 at step inc1, VCO2 at inc2. The caller supplies
 * pitch increments from a table on the unit or pow() on the host. */
void ob1_voice_render(struct ob1_voice *v, const struct ob1_knobs *k, uint32_t inc1, uint32_t inc2, int32_t *out);

#endif
