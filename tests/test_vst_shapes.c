/* SPDX-License-Identifier: GPL-2.0-or-later */
/* OB-2 by KBPlugs: cycle means and relative RMS from 48 kHz, A3, VCO1 dense captures.
 * Phase and polarity are free; time reversal is deliberately not allowed. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "voice.h"
#define BINS 64
static const struct { int wave; double level; double cycle[BINS]; } measured[] = {
    {0, 0.016813, {
        0.799244, 0.889298, 0.976805, 1.061809, 1.144358, 1.224480, 1.302227, 1.377635,
        1.450741, 1.521582, 1.590217, 1.651190, 1.595146, 1.446533, 1.300797, 1.158304,
        1.019012, 0.882912, 0.749947, 0.620133, 0.493364, 0.369648, 0.248972, 0.131266,
        0.016476, -0.095378, -0.204390, -0.310548, -0.413941, -0.514574, -0.612511, -0.707790,
        -0.800418, -0.890477, -0.977974, -1.062992, -1.145527, -1.225651, -1.303402, -1.378800,
        -1.451907, -1.522757, -1.591392, -1.652369, -1.596306, -1.447677, -1.301969, -1.159479,
        -1.020175, -0.884099, -0.751146, -0.621294, -0.494537, -0.370828, -0.250144, -0.132431,
        -0.017658, 0.094209, 0.203223, 0.309388, 0.412769, 0.513406, 0.611338, 0.706608,
    }},
    {16, -2.431843, {
        -0.190368, -0.097755, -0.007763, 0.079650, 0.164545, 0.246946, 0.326927, 0.404493,
        0.479731, 0.552654, 0.623305, 0.691736, 0.757971, 0.822098, 0.884108, 0.944057,
        1.001997, 1.057945, 1.131479, 1.875138, 2.091555, 1.920575, 1.754793, 1.592502,
        1.433568, 1.277973, 1.125649, 0.976592, 0.830749, 0.688094, 0.548545, 0.412106,
        0.278746, 0.148413, 0.021059, -0.103374, -0.224872, -0.343523, -0.459364, -0.572395,
        -0.682698, -0.790304, -0.895251, -0.997569, -1.097301, -1.194513, -1.289198, -1.381445,
        -1.471266, -1.558688, -1.642563, -1.646974, -1.522325, -1.394878, -1.270442, -1.149128,
        -1.030916, -0.915707, -0.803489, -0.694231, -0.587862, -0.484338, -0.383599, -0.285636,
    }},
    {32, -3.638256, {
        -0.014454, 0.008734, 0.030658, 0.202664, 2.235143, 2.507281, 2.348765, 2.186828,
        2.027537, 1.871132, 1.717534, 1.566747, 1.418762, 1.273494, 1.130927, 0.991042,
        0.853818, 0.719194, 0.587149, 0.457683, 0.330720, 0.206211, 0.084221, -0.035378,
        -0.152594, -0.267440, -0.379956, -0.490149, -0.598102, -0.703802, -0.807306, -0.908660,
        -1.007823, -1.104892, -1.199859, -1.289618, -1.295586, -1.224995, -1.155968, -1.089118,
        -1.024405, -0.961790, -0.901235, -0.842692, -0.786147, -0.731508, -0.678808, -0.627934,
        -0.578914, -0.531682, -0.486196, -0.442424, -0.400342, -0.359906, -0.321077, -0.283829,
        -0.248142, -0.213957, -0.181247, -0.150011, -0.120171, -0.091734, -0.064649, -0.038900,
    }},
    {48, -2.306452, {
        1.222422, 1.120447, 1.019977, 0.920981, 0.823444, 0.727349, 0.632689, 0.539444,
        0.447599, 0.357143, 0.268062, 0.180332, 0.093970, 0.008920, -0.074822, -0.157253,
        -0.238397, -0.318269, -0.396884, -0.474237, -0.550273, -0.608701, -0.620260, -0.627302,
        -0.634841, -0.642906, -0.651483, -0.660575, -0.670145, -0.680185, -0.690682, -0.701633,
        -0.713015, -0.724796, -0.736986, -0.749577, -0.762524, -0.775842, -0.789516, -0.803526,
        -0.817865, -0.832503, -0.847464, -0.862704, -0.878221, -0.894027, -0.910065, -0.926369,
        -0.942896, -0.959656, -0.976641, -0.993827, -0.995388, 0.719279, 2.356397, 2.210972,
        2.093667, 1.979200, 1.866341, 1.755133, 1.645497, 1.537415, 1.430884, 1.325885,
    }},
    {63, 0.000000, {
        1.108064, 1.042328, 0.977208, 0.912673, 0.848731, 0.785400, 0.722640, 0.660483,
        0.598906, 0.537927, 0.477524, 0.417692, 0.358453, 0.299790, 0.241692, 0.184169,
        0.127219, 0.070833, 0.015022, -0.040237, -0.094921, -0.149056, -0.202621, -0.255643,
        -0.308118, -0.360024, -0.411395, -0.462207, -0.512483, -0.562200, -0.611398, -0.660060,
        -0.708169, -0.755743, -0.802795, -0.849310, -0.895300, -0.940763, -0.985703, -1.030117,
        -1.074010, -1.117392, -1.160255, -1.202601, -1.244441, -1.285770, -1.326587, -1.366899,
        -1.406714, -1.446020, -1.484832, -1.511262, 0.088379, 1.903528, 1.800263, 1.727086,
        1.655817, 1.585186, 1.515188, 1.445812, 1.377041, 1.308889, 1.241343, 1.174397,
    }}
};
/* Standalone and paired full-saw evaluators must agree sample for sample. */
static void test_full_saw_fast_path(void)
{
    const uint32_t increments[] = {1u << 15, 19685266u, 1u << 28, DSP_INC_MAX};
    unsigned k;
    for (k = 0; k < sizeof(increments) / sizeof(increments[0]); k++) {
        struct ob1_voice a, b;
        struct dsp_shape saw;
        int32_t fast[32], fs[32], generic[32], gs[32], other[32], os[32];
        int block, i;
        ob1_voice_init(&a, 1); ob1_voice_init(&b, 1);
        dsp_shape_saw(&saw, 32768);
        for (block = 0; block < 100; block++) {
            dsp_osc_block(&a.vco1, increments[k], &saw, fast, fs, 32);
            dsp_pair(&b.vco1, &b.vco2, increments[k], increments[k], &saw, &saw,
                     0, generic, gs, other, os, 32);
            for (i = 0; i < 32; i++) { assert(fast[i] == generic[i]); assert(fs[i] == gs[i]); }
        }
    }
}

/* Golden hashes from the generic full-saw carrier, before adding paired fast paths. */
static void test_full_saw_pair_golden(void)
{
    static const uint32_t increments[3][2] = {
        {2926235u, 4389432u}, {19685266u, 29494959u}, {157482134u, 235959677u}
    };
    static const uint32_t expected[4][3] = {{0x8f88db6au,0xc1f19a2cu,0xeeb379dau},{0x6b86055fu,0x6486e294u,0x56779a0bu},{0xe7713475u,0xbfc48d35u,0x88efe863u},{0xec36dd62u,0x4fb6060cu,0x39bb8b30u}};
    struct dsp_shape carrier, source;
    int mod, shape, block, i, j;
    dsp_shape_saw(&carrier, 32768);
    for (mod = 0; mod < 4; mod++)
        for (shape = 0; shape < 3; shape++) {
            struct dsp_osc a = {{0}, 0, 32767, 32767}, b = a;
            int32_t output[4][32];
            uint32_t hash = 2166136261u;
            a.vco.phase = 0x12345678u; b.vco.phase = 0x98765432u;
            if (!shape) dsp_shape_saw(&source, -12343);
            else dsp_shape_pulse(&source, shape == 1 ? 0x80000000u : 287762809u);
            for (block = 0; block < 100; block++) {
                dsp_pair(&a, &b, increments[shape][0], increments[shape][1], &carrier, &source,
                         mod, output[0], output[1], output[2], output[3], 32);
                for (i = 0; i < 32; i++)
                    for (j = 0; j < 4; j++)
                        hash = (hash ^ (uint32_t)output[j][i]) * 16777619u;
            }
            assert(hash == expected[mod][shape]);
        }
}

int main(void)
{
    test_full_saw_fast_path();
    test_full_saw_pair_golden();
    const uint32_t inc = (uint32_t)(220.0 / 48000 * 4294967296.0);
    double rms[5];
    unsigned c;
    for (c = 0; c < sizeof(measured) / sizeof(measured[0]); c++) {
        struct ob1_voice voice;
        struct ob1_knobs k = {measured[c].wave, 63, 6, 0, 0, 0};
        double bins[BINS] = {0}, energy = 0, norm = 0, mean = 0, best = 100;
        unsigned counts[BINS] = {0};
        int32_t out[32];
        uint32_t phase = 0;
        int block, i, shift, sign;
        ob1_voice_init(&voice, 1);
        for (block = 0; block < 6000; block++) {
            ob1_voice_render(&voice, &k, inc, inc, out);
            for (i = 0; i < 32; i++) {
                unsigned bin = phase >> 26;
                phase += inc;
                if (block < 1500) continue;
                bins[bin] += out[i]; counts[bin]++;
                energy += (double)out[i] * out[i];
            }
        }
        rms[c] = sqrt(energy / (4500 * 32));
        for (i = 0; i < BINS; i++) { bins[i] /= counts[i]; mean += bins[i] / BINS; }
        for (i = 0; i < BINS; i++) { bins[i] -= mean; norm += bins[i] * bins[i] / BINS; }
        norm = sqrt(norm);
        for (i = 0; i < BINS; i++) bins[i] /= norm;
        for (sign = -1; sign <= 1; sign += 2)
            for (shift = 0; shift < BINS; shift++) {
                double error = 0;
                for (i = 0; i < BINS; i++) {
                    double d = bins[(i + shift) % BINS] * sign - measured[c].cycle[i];
                    error += d * d / BINS;
                }
                if (error < best) best = error;
            }
        printf("WAVE %d measured cycle RMSE %.4f\n", k.wav1, sqrt(best));
        assert(sqrt(best) < .25);
    }
    for (c = 0; c < sizeof(measured) / sizeof(measured[0]); c++) {
        double error = 20 * log10(rms[c] / rms[4]) - measured[c].level;
        printf("WAVE %d relative level error %.3f dB\n", measured[c].wave, error);
        assert(fabs(error) < .2);
    }
    return 0;
}
