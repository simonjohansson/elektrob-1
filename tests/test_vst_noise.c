/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "voice.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

/* Broadband regression anchor of the filter fitted to the reference's 200-8000 Hz level. */
static void test_calibrated_level(void)
{
    struct ob1_voice noise, saw;
    struct ob1_knobs nk = {0, 0, 0, 0, 0, 2}, sk = {63, 63, 6, 0, 0, 0};
    int32_t n[32], s[32];
    const uint32_t inc = (uint32_t)(220.0 * 4294967296.0 / 48000);
    double np = 0, sp = 0;
    int block, i;
    ob1_voice_init(&noise, 1); ob1_voice_init(&saw, 1);
    for (block = 0; block < 2400; block++) {
        ob1_voice_render(&noise, &nk, inc, inc, n);
        ob1_voice_render(&saw, &sk, inc, inc, s);
        if (block < 300) continue;
        for (i = 0; i < 32; i++) { np += (double)n[i] * n[i]; sp += (double)s[i] * s[i]; }
    }
    double db = 10 * log10(np / sp);
    assert(fabs(db + 17.54) < .15);
    printf("Noise: calibrated broadband level %.3f dB relative to full saw\n", db);
}

int main(void)
{
    test_calibrated_level();
    struct ob1_voice v, split;
    struct ob1_knobs k = {0};
    int32_t out[32], twice[32], zero[32] = {0};
    const int32_t *src[4] = {zero, zero, zero, zero};
    const int32_t gain[4] = {0};
    double energy = 0, lag = 0, previous = 0;
    int block, i;
    k.nois = 2;
    ob1_voice_init(&v, 12345);
    ob1_voice_init(&split, 12345);
    for (block = 0; block < 4096; block++) {
        dsp_voice_out(&v.hpf, src, gain, &v.noise, 512, out, 32);
        dsp_voice_out(&split.hpf, src, gain, &split.noise, 512, twice, 16);
        dsp_voice_out(&split.hpf, src, gain, &split.noise, 512, twice + 16, 16);
        for (i = 0; i < 32; i++) {
            double x = out[i] / 65536.0;
            assert(out[i] == twice[i]);
            energy += x*x;
            lag += x*previous;
            previous = x;
        }
    }
    /* The reference's gentle high-frequency rolloff requires correlated white output. */
    assert(lag / energy > 0.28 && lag / energy < 0.34);
    i = v.hpf.noise_lp;
    dsp_voice_out(&v.hpf, src, gain, &v.noise, 0, out, 32);
    assert(v.hpf.noise_lp == i);
    ob1_voice_render(&v, &k, 0, 0, out);
    assert(v.hpf.noise_lp != i);
    printf("Noise: block continuity, spectral coloration and disabled state passed (lag %.4f)\n", lag/energy);
    return 0;
}
