/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "voice.h"

/* A mixer setting, 0-2 (OFF, -3 dB, ON), as its gain. */
static const int32_t mix_gain[3] = {0, DSP_MIX_LOW, DSP_MIX_HIGH};
static const int32_t noise_gain[3] = {0, DSP_MIX_NOISE_LOW * DSP_NOISE_LEVEL >> 12, DSP_MIX_NOISE_HIGH * DSP_NOISE_LEVEL >> 12};
static const int32_t zero[32];                 /* a source not rendered */

/* WAV1 and WAV2 0-63: saw type, triangle (0) to saw (63), as g -1 to +1, Q15. */
static int32_t wave_g(int32_t w)
{
    return -32768 + (w << 16) / 63;
}

/* WAV1 and WAV2 64-127: pulse type, from the narrowest pulse (64) to the square (127), as the OB-1's WAVE pot
 * turned clockwise ends on the square (trimmed to 50 %). The 6.7 % minimum is provisional;
 * the schematic leaves it to unprinted operating points. */
#define PW_MIN  287762809u                         /* 6.7 % of 2^32 */
static uint32_t wave_width(int32_t w)
{
    return PW_MIN + (uint32_t)(w - 64) * ((0x80000000u - PW_MIN) / 63);
}

void ob1_shape(struct dsp_shape *sh, int32_t w)
{
    if (w < 64)
        dsp_shape_saw(sh, wave_g(w));
    else
        dsp_shape_pulse(sh, wave_width(w));
}

/* An oscillator at rest: its ramp at 0, its flip-flop high. Field by field: a struct copy would call
 * memcpy, which a mod does not have. */
static void osc_init(struct dsp_osc *o)
{
    o->vco.phase = o->vco.inc = o->vco.rinc = o->vco.shift = 0;
    o->prev = 0;
    o->sub = o->sub_prev = 32767;
}

void ob1_voice_init(struct ob1_voice *v, uint32_t seed)
{
    int i;
    osc_init(&v->vco1);
    osc_init(&v->vco2);
    v->hpf.lp = 0;
    for (i = 0; i < 4; i++)
        v->hpf.source[i] = 0;
    v->noise = seed ? seed : 1;
}

void ob1_voice_render(struct ob1_voice *v, const struct ob1_knobs *k, uint32_t inc1, uint32_t inc2, int32_t *out)
{
    int32_t f1[32], s1[32], f2[32], s2[32], gain[4];
    int32_t mix1 = k->mix1, mix2 = k->mix2, mod = k->mod;
    const int32_t *src[4];
    struct dsp_shape sh1, sh2;
    ob1_shape(&sh1, k->wav1);
    ob1_shape(&sh2, k->wav2);
    if (!(mod & DSP_XMOD) || !mix1)
        mod &= ~DSP_XMOD;                                          /* moves only VCO1 */
    if (!mix2)
        mod &= ~DSP_SYNC;                                          /* moves only VCO2 */
    if (mod)
        dsp_pair(&v->vco1, &v->vco2, inc1, inc2, &sh1, &sh2, mod, f1, s1, f2, s2, 32);
    else {                                                         /* each alone, or not at all */
        if (mix1)
            dsp_osc_block(&v->vco1, inc1, &sh1, f1, s1, 32);
        if (mix2)
            dsp_osc_block(&v->vco2, inc2, &sh2, f2, s2, 32);
    }
    src[0] = mix1 ? f1 : zero;                                     /* FUND and OCT, each VCO */
    src[1] = mix1 ? s1 : zero;
    src[2] = mix2 ? f2 : zero;
    src[3] = mix2 ? s2 : zero;
    gain[0] = mix_gain[mix1 / 3];
    gain[1] = mix_gain[mix1 % 3];
    gain[2] = mix_gain[mix2 / 3];
    gain[3] = mix_gain[mix2 % 3];
    dsp_voice_out(&v->hpf, src, gain, &v->noise, noise_gain[k->nois], out, 32);
}
