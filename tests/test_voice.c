/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Shared voice mechanics, routing and output policy. */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "voice.h"

#define RATE 48000

static uint32_t step(double hz)
{
    return (uint32_t)(hz * 4294967296.0 / RATE);
}

/* Middle source levels remain approximately -3 dB across shapes and pitches. */
static void test_mix_levels(void)
{
    const int waves[] = {0, 32, 63, 64, 127};
    const double hz[] = {55, 220, 1760};
    int oscillator, sub, wave, pitch, level, block, i, cases = 0;
    double worst = 0;
    for (oscillator = 0; oscillator < 2; oscillator++)
        for (sub = 0; sub < 2; sub++)
            for (wave = 0; wave < 5; wave++)
                for (pitch = 0; pitch < 3; pitch++) {
                    double power[2] = {0};
                    uint32_t inc = (uint32_t)(hz[pitch] * 4294967296.0 / 48000);
                    for (level = 1; level <= 2; level++) {
                        struct ob1_voice v;
                        struct ob1_knobs k = {waves[wave], waves[wave], 0, 0, 0, 0};
                        int32_t out[32];
                        if (oscillator) k.mix2 = level * (sub ? 1 : 3);
                        else k.mix1 = level * (sub ? 1 : 3);
                        ob1_voice_init(&v, 1);
                        for (block = 0; block < 1800; block++) {
                            ob1_voice_render(&v, &k, inc, inc, out);
                            if (block < 300) continue;
                            for (i = 0; i < 32; i++) power[level - 1] += (double)out[i] * out[i];
                        }
                    }
                    double error = fabs(10 * log10(power[0] / power[1]) + 3);
                    assert(error < .025);
                    if (error > worst) worst = error;
                    cases++;
                }
    {
        double power[2] = {0};
        for (level = 1; level <= 2; level++) {
            struct ob1_voice v;
            struct ob1_knobs k = {0, 0, 0, 0, 0, level};
            int32_t out[32];
            ob1_voice_init(&v, 1);
            for (block = 0; block < 1800; block++) {
                ob1_voice_render(&v, &k, 0, 0, out);
                if (block < 300) continue;
                for (i = 0; i < 32; i++) power[level - 1] += (double)out[i] * out[i];
            }
        }
        assert(fabs(10 * log10(power[0] / power[1]) + 3) < .025);
    }
    printf("Mixer: %d oscillator/sub cases and noise match -3 dB, worst error %.4f dB\n", cases, worst);
}

static void test_noise_output(void)
{
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
}

static void test_headroom_and_additivity(void)
{
    const int shapes[] = {0, 32, 63, 64, 100, 127};
    const double frequencies[] = {27.5, 55, 220, 1900, 12000};
    int shape, f, mod, variant, block, i;
    int64_t peak = 0;
    for (shape = 0; shape < 6; shape++)
        for (f = 0; f < 5; f++)
            for (mod = 0; mod < 4; mod++)
                for (variant = 0; variant < 2; variant++) {
                    struct ob1_voice v;
                    struct ob1_knobs k = {shapes[shape], shapes[variant ? 5 - shape : shape], 8, 8, mod, 2};
                    int32_t out[32];
                    ob1_voice_init(&v, 1);
                    for (block = 0; block < 1500; block++) {
                        ob1_voice_render(&v, &k, step(frequencies[f]),
                                         step(fmin(frequencies[f] * (variant ? 1.5 : 1), 12000)), out);
                        for (i = 0; i < 32; i++) {
                            int64_t magnitude = out[i] < 0 ? -(int64_t)out[i] : out[i];
                            assert(out[i] > INT32_MIN && out[i] < INT32_MAX);
                            if (magnitude > peak)
                                peak = magnitude;
                        }
                    }
                }
    /* Unchanged engine peaks at -4.28 dBFS in this matrix; allow 0.03 dB rounding margin. */
    assert(20 * log10(peak / 2147483648.0) <= -4.25);
    /* Equal sources remain additive rather than normalized by source count. */
    {
        struct ob1_voice one, two;
        struct ob1_knobs k = {63, 63, 8, 0, 0, 0};
        int32_t a[32], b[32];
        double power_one = 0, power_two = 0;
        ob1_voice_init(&one, 1);
        ob1_voice_init(&two, 1);
        for (block = 0; block < 1000; block++) {
            k.mix2 = 0;
            ob1_voice_render(&one, &k, step(220), step(220), a);
            k.mix2 = 8;
            ob1_voice_render(&two, &k, step(220), step(220), b);
            for (i = 0; i < 32; i++) {
                power_one += (double)a[i] * a[i];
                power_two += (double)b[i] * b[i];
            }
        }
        assert(fabs(10 * log10(power_two / power_one) - 6.0206) < 0.01);
    }
    printf("voice: 240 maximum-mix cases passed, peak %.2f dBFS\n", 20 * log10(peak / 2147483648.0));
}

/* The mixer removes DC, ignores muted sources and saturates instead of wrapping. */
static void test_mix_out(void)
{
    static int32_t acc[RATE], out[RATE], junk[RATE];
    struct dsp_hpf h = {0};
    uint32_t noise = 1;
    const int32_t *src[4] = {acc, junk, junk, junk};
    int32_t high[4] = {DSP_MIX_HIGH, 0, 0, 0}, i;
    double tail = 0;
    for (i = 0; i < RATE; i++) {
        acc[i] = 10000 + ((i / 24) & 1 ? 20000 : -20000);   /* 1 kHz square riding on DC */
        junk[i] = 65535;
    }
    dsp_voice_out(&h, src, high, &noise, 0, out, RATE);
    for (i = RATE - 4800; i < RATE; i++)
        tail += out[i];
    assert(fabs(tail / 4800 / 2147483648.0) < 0.001);           /* the DC is gone */
    /* the square's edge at a quarter, less the 11 Hz high-pass's droop over 24 samples (3.4 %) */
    assert(abs((out[RATE - 1] - out[RATE - 25]) / 65536 - 2 * 5000) < 400);
    for (i = 0; i < RATE; i++)
        acc[i] = junk[i] = (i & 1) ? 65535 : -65535;          /* four sources at their loudest: too loud */
    for (i = 0; i < 4; i++)
        high[i] = DSP_MIX_HIGH;
    src[1] = src[2] = src[3] = junk;
    dsp_voice_out(&h, src, high, &noise, DSP_MIX_NOISE_HIGH, out, 64);
    for (i = 0; i < 64; i++)
        assert(out[i] == ((i & 1) ? INT32_MAX : INT32_MIN));
}

/* Pulse controls end at square; audible source shaping must not alter raw-ramp XMOD. */
static void test_wave_mapping_and_xmod_source(void)
{
    struct dsp_shape shape, carrier;
    int32_t f1[32], s1[32], f2[32], s2[32], reference[32 * 32];
    uint32_t previous = 0;
    int wave, block, i;
    dsp_shape_saw(&carrier, 32768);
    for (wave = 64; wave < 128; wave++) {
        ob1_shape(&shape, wave);
        assert(shape.pulse && shape.width > previous && shape.width <= 0x80000000u);
        previous = shape.width;
    }
    ob1_shape(&shape, 64);
    assert(fabs(shape.width / 4294967296.0 - 0.067) < 1e-8);
    assert(fabs(previous / 4294967296.0 - 0.5) < 1e-7);
    for (wave = 0; wave < 128; wave++) {
        struct dsp_osc a = {{0}, 0, 32767, 32767}, b = a;
        ob1_shape(&shape, wave);
        for (block = 0; block < 32; block++) {
            dsp_pair(&a, &b, step(200), step(2), &carrier, &shape, DSP_XMOD,
                     f1, s1, f2, s2, 32);
            for (i = 0; i < 32; i++) {
                if (!wave)
                    reference[block * 32 + i] = f1[i];
                else
                    assert(f1[i] == reference[block * 32 + i]);
            }
        }
    }
}

/* Mixer levels must not affect oscillator clocks, sub-octaves or modulation history. */
static void test_modulation_routing(void)
{
    int mode, mix, block, i, different = 0;
    int32_t out[32], other[32];
    struct ob1_voice muted, audible, xmod;
    struct ob1_knobs k = {63, 127, 6, 0, 0, 0}, a;
    for (mode = 0; mode < 4; mode++) {
        ob1_voice_init(&muted, 1);
        ob1_voice_init(&audible, 1);
        k.mod = mode;
        a = k;
        a.mix2 = 8;
        for (block = 0; block < 200; block++) {
            ob1_voice_render(&muted, &k, step(220), step(330), out);
            ob1_voice_render(&audible, &a, step(220), step(330), other);
            assert(!memcmp(&muted.vco1, &audible.vco1, sizeof muted.vco1));
            assert(!memcmp(&muted.vco2, &audible.vco2, sizeof muted.vco2));
        }
    }
    ob1_voice_init(&muted, 1);
    ob1_voice_init(&xmod, 1);
    for (block = 0; block < 200; block++) {
        k.mod = DSP_SYNC | DSP_XMOD;
        ob1_voice_render(&muted, &k, step(220), step(330), out);
        k.mod = DSP_XMOD;
        ob1_voice_render(&xmod, &k, step(220), step(330), other);
        for (i = 0; i < 32; i++)
            different |= out[i] != other[i];
    }
    assert(different);
    ob1_voice_init(&muted, 1);
    ob1_voice_init(&audible, 1);
    for (block = 0; block < 400; block++) {
        uint32_t inc1 = step(block < 200 ? 220 : 330), inc2 = step(261.625565);
        mix = block % 9;
        mode = block / 100;
        k.mix1 = mix;
        k.mix2 = (block / 9) % 9;
        k.mod = mode;
        k.wav1 = block % 128;
        k.wav2 = 127 - k.wav1;
        a = k;
        a.mix1 = a.mix2 = 8;
        ob1_voice_render(&muted, &k, inc1, inc2, out);
        ob1_voice_render(&audible, &a, inc1, inc2, other);
        assert(!memcmp(&muted.vco1, &audible.vco1, sizeof muted.vco1));
        assert(!memcmp(&muted.vco2, &audible.vco2, sizeof muted.vco2));
    }
}

static void test_mixer_phase_continuity(void)
{
    struct ob1_voice v;
    struct ob1_knobs k = {0, 0, 6, 3, 0, 0};
    const uint32_t inc = step(261.625565);
    int32_t out[32];
    double low = 1e100, high = 0;
    ob1_voice_init(&v, 1);
    for (int repeat = 0; repeat < 6; repeat++) {
        k.mix2 = 0;
        for (int block = 0; block < 997 + 137 * repeat; block++) {
            ob1_voice_render(&v, &k, inc, inc, out);
            assert(v.vco1.vco.phase == v.vco2.vco.phase);
            assert(v.vco1.sub == v.vco2.sub);
        }
        k.mix2 = 3;
        double power = 0;
        for (int block = 0; block < 900; block++) {
            ob1_voice_render(&v, &k, inc, inc, out);
            if (block >= 150)
                for (int i = 0; i < 32; i++)
                    power += (double)out[i] * out[i];
        }
        low = fmin(low, power);
        high = fmax(high, power);
    }
    assert(10 * log10(high / low) < 0.02);
}

int main(void)
{
    test_mix_levels();
    test_noise_output();
    test_headroom_and_additivity();
    test_mix_out();
    test_wave_mapping_and_xmod_source();
    test_modulation_routing();
    test_mixer_phase_continuity();
    puts("voice: all regressions passed");
    return 0;
}
