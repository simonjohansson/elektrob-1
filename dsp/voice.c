/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "voice.h"

/* Constant source gains include the fixed output trim before summing and saturation. */
static const int32_t mix_gain[3] = {0, DSP_MIX_LOW * DSP_OUTPUT_GAIN >> 12, DSP_MIX_HIGH * DSP_OUTPUT_GAIN >> 12};
static const int32_t noise_gain[3] = {0, ((DSP_MIX_NOISE_LOW * DSP_NOISE_LEVEL) >> 12) * DSP_OUTPUT_GAIN >> 12,
                                      ((DSP_MIX_NOISE_HIGH * DSP_NOISE_LEVEL) >> 12) * DSP_OUTPUT_GAIN >> 12};

/* Reference dense control captures: ramp-half gain and relative level, Q15/Q12. */
#define SAW(g, level) {g, {0, ((DSP_MIX_LOW * DSP_OUTPUT_GAIN >> 12) * (level)) >> 12, \
                          ((DSP_MIX_HIGH * DSP_OUTPUT_GAIN >> 12) * (level)) >> 12}}
static const struct { int32_t g, fund[3]; } saw_shape[64] = {
    SAW(-32768, 8208), SAW(-32652, 8075), SAW(-32175, 7988), SAW(-31830, 7882),
    SAW(-31532, 7773), SAW(-31059, 7685), SAW(-30855, 7568), SAW(-30580, 7463),
    SAW(-30289, 7356), SAW(-29865, 7265), SAW(-29427, 7177), SAW(-29024, 7087),
    SAW(-28568, 7004), SAW(-28095, 6923), SAW(-27430, 6857), SAW(-26869, 6784),
    SAW(-26115, 6722), SAW(-25475, 6656), SAW(-24789, 6591), SAW(-23949, 6536),
    SAW(-23085, 6478), SAW(-22350, 6417), SAW(-21498, 6363), SAW(-20742, 6302),
    SAW(-19919, 6245), SAW(-18925, 6187), SAW(-18097, 6131), SAW(-17128, 6074),
    SAW(-16124, 6017), SAW(-15164, 5959), SAW(-14176, 5902), SAW(-13078, 5844),
    SAW(-12343, 5803), SAW(-11097, 5730), SAW(-10090, 5677), SAW(-9060, 5626),
    SAW(-8039, 5577), SAW(-6733, 5505), SAW(-5703, 5463), SAW(-4581, 5409),
    SAW(-3263, 5345), SAW(-1917, 5274), SAW(-813, 5232), SAW(425, 5180),
    SAW(1889, 5110), SAW(2990, 5075), SAW(4388, 5016), SAW(5801, 4959),
    SAW(7096, 4915), SAW(9015, 4817), SAW(10343, 4775), SAW(11350, 4769),
    SAW(12952, 4707), SAW(14456, 4660), SAW(16723, 4550), SAW(17648, 4555),
    SAW(19935, 4453), SAW(21767, 4393), SAW(23236, 4362), SAW(25323, 4290),
    SAW(26967, 4253), SAW(28565, 4219), SAW(30455, 4170), SAW(32768, 4096),
};
#undef SAW

/* Reference source attenuation: phase increment >> 14 (0.183 Hz/step), amplitude Q15. */
static const struct { int32_t pitch, gain; } pitch_gain[] = {
    {1202, 32768}, {9675, 25775}, {24588, 14391}, {29301, 11270},
    {32936, 9061}, {37025, 6836}, {39256, 5737}, {41623, 4669},
    {44132, 3650}, {46794, 2697}, {49617, 1838}, {52610, 1103},
    {55785, 525}, {59153, 142},
};

static inline __attribute__((always_inline)) int32_t fundamental_gain(int32_t gain, uint32_t inc)
{
    int32_t pitch = inc >> 14, scale = pitch_gain[sizeof pitch_gain / sizeof *pitch_gain - 1].gain;
    if (!gain || pitch <= pitch_gain[0].pitch)
        return gain;
    if (pitch < pitch_gain[sizeof pitch_gain / sizeof *pitch_gain - 1].pitch) {
        unsigned i = 1;
        while (pitch >= pitch_gain[i].pitch) i++;
        int32_t span = pitch_gain[i].pitch - pitch_gain[i - 1].pitch;
        scale = pitch_gain[i - 1].gain -
            (pitch_gain[i - 1].gain - pitch_gain[i].gain) *
            (pitch - pitch_gain[i - 1].pitch) / span;
    }
    /* shortcut: clamp above the last measured pitch; extend only with verified reference data. */
    return (gain * scale + 16384) >> 15;
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
        dsp_shape_saw(sh, saw_shape[w].g);
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
    osc_init(&v->vco1);
    osc_init(&v->vco2);
    v->hpf.lp = 0;
    v->hpf.noise_lp = 0;
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
    /* Mixer switches gate audio, not the oscillator clocks or modulation paths. */
    if (mod)
        dsp_pair(&v->vco1, &v->vco2, inc1, inc2, &sh1, &sh2, mod, f1, s1, f2, s2, 32);
    else {
        dsp_osc_block(&v->vco1, inc1, &sh1, f1, s1, 32);
        dsp_osc_block(&v->vco2, inc2, &sh2, f2, s2, 32);
    }
    src[0] = f1;                                                   /* FUND and OCT, each VCO */
    src[1] = s1;
    src[2] = f2;
    src[3] = s2;
    gain[0] = k->wav1 < 64 ? saw_shape[k->wav1].fund[mix1 / 3] : mix_gain[mix1 / 3];
    gain[1] = mix_gain[mix1 % 3];
    gain[2] = k->wav2 < 64 ? saw_shape[k->wav2].fund[mix2 / 3] : mix_gain[mix2 / 3];
    gain[3] = mix_gain[mix2 % 3];
    gain[0] = fundamental_gain(gain[0], inc1);
    gain[2] = fundamental_gain(gain[2], inc2);
    dsp_voice_out(&v->hpf, src, gain, &v->noise, noise_gain[k->nois], out, 32);
}
