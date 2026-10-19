/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Shared voice mechanics, routing and output policy. */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "voice.h"
#include "output.h"

#define RATE 48000

static uint32_t step(double hz)
{
    return (uint32_t)(hz * 4294967296.0 / RATE);
}

/* Freeze the approved rendered model, including signed phase, morph levels and pitch attenuation. */
static void test_waveform_golden(void)
{
    static const double hz[] = {55, 220, 880};
    static const int waves[] = {0, 16, 32, 48, 63, 64, 80, 96, 112, 127};
    static const uint32_t expected[3][10] = {
        {0xbfd1fa41u, 0x06032e15u, 0x9f622005u, 0x573ec3cfu, 0xa019e57du, 0x17f35800u, 0xa53f0dafu, 0x917ab749u, 0x6b4a66d2u, 0x2e713ea8u},
        {0x4bee26cfu, 0x5daedeedu, 0x97e4694cu, 0xe9b9e5afu, 0x4fb97e2du, 0x5e5cccd6u, 0x0acad70au, 0x45ddb8acu, 0x56a9463fu, 0x0171c910u},
        {0xbebafa19u, 0x557e12e5u, 0x987d0d49u, 0x2486e3aau, 0xdf100628u, 0xbd063103u, 0x50d787c1u, 0x8b208a17u, 0x423760aeu, 0xaa07ee5fu},
    };
    for (int oscillator = 0; oscillator < 2; oscillator++)
        for (int pitch = 0; pitch < 3; pitch++)
            for (int wave = 0; wave < 10; wave++) {
                struct ob1_voice v;
                struct ob1_knobs k = {waves[wave], waves[wave], oscillator ? 0 : 6, oscillator ? 6 : 0, 0, 0};
                uint32_t hash = 2166136261u, inc = step(hz[pitch]);
                int32_t output[32];
                ob1_voice_init(&v, 1);
                for (int block = 0; block < 256; block++) {
                    ob1_voice_render(&v, &k, inc, inc, output);
                    for (int i = 0; i < 32; i++)
                        for (int shift = 0; shift < 32; shift += 8)
                            hash = (hash ^ (((uint32_t)output[i] >> shift) & 255u)) * 16777619u;
                }
                if (hash != expected[pitch][wave])
                    fprintf(stderr, "waveform VCO%d %.0f Hz W%d: %08x, expected %08x\n",
                            oscillator + 1, hz[pitch], waves[wave], (unsigned)hash, (unsigned)expected[pitch][wave]);
                assert(hash == expected[pitch][wave]);
            }
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

/* Independent source-level measurements, including withheld A4/A5 pitches and the upper clamp. */
static void test_pitch_gain(void)
{
    const double hz[] = {55, 220, 440, 880, 1771.521389, 4502.277746, 7188.007958, 10831.212292, 12000};
    const double level[] = {1, 1, .969393, .908247, .786590, .439169, .175093, .004348, .004348};
    const int waves[] = {0, 63, 127}, gains[] = {2595, 1295, 1295};
    double worst = 0;
    for (int oscillator = 0; oscillator < 2; oscillator++)
        for (int wave = 0; wave < 3; wave++)
            for (int pitch = 0; pitch < 9; pitch++)
                for (int mod = 0; mod < 4; mod++) {
                    struct ob1_voice v, raw;
                    struct ob1_knobs k = {waves[wave], waves[wave], oscillator ? 0 : 6, oscillator ? 6 : 0, mod, 0};
                    struct dsp_shape sh;
                    int32_t f1[32], s1[32], f2[32], s2[32], out[32], reference[32];
                    const int32_t *src[] = {f1, s1, f2, s2};
                    int32_t gain[4] = {0};
                    uint32_t inc1 = step(oscillator ? 330 : hz[pitch]);
                    uint32_t inc2 = step(oscillator ? hz[pitch] : 660);
                    double dot = 0, power = 0;
                    double history[2] = {0};
                    gain[2 * oscillator] = gains[wave];
                    ob1_voice_init(&v, 1);
                    ob1_voice_init(&raw, 1);
                    ob1_shape(&sh, waves[wave]);
                    for (int block = 0; block < 400; block++) {
                        ob1_voice_render(&v, &k, inc1, inc2, out);
                        if (mod)
                            ob1_dsp_pair(&raw.vco1, &raw.vco2, inc1, inc2, &sh, &sh, mod, f1, s1, f2, s2, 32);
                        else {
                            ob1_dsp_osc_block(&raw.vco1, inc1, &sh, f1, s1, 32);
                            ob1_dsp_osc_block(&raw.vco2, inc2, &sh, f2, s2, 32);
                        }
                        assert(!memcmp(&v.vco1, &raw.vco1, sizeof v.vco1));
                        assert(!memcmp(&v.vco2, &raw.vco2, sizeof v.vco2));
                        ob1_dsp_voice_out(&raw.hpf, src, gain, &raw.noise, 0, reference, 32);
                        for (int i = 0; i < 32; i++) {
                            double x = reference[i] / 65536.0;
                            reference[i] = (int32_t)(floor((x + 2 * history[0] + history[1]) / 4) * 65536);
                            history[1] = history[0];
                            history[0] = x;
                        }
                        if (block < 100) continue;
                        for (int i = 0; i < 32; i++) {
                            dot += (double)out[i] * reference[i];
                            power += (double)reference[i] * reference[i];
                            if (pitch < 2) assert(out[i] == reference[i]);
                        }
                    }
                    /* Project onto the filtered reference so integer DC residue is not gain. */
                    assert(power > 0 && fabs(dot / power - level[pitch]) < .0015);
                    worst = fmax(worst, fabs(dot / power - level[pitch]));
                }
    printf("Pitch gain: 216 waveform/pitch/modulation cases passed, maximum gain error %.6f\n", worst);
}

/* Compare streaming output against a whole-buffer convolution, including signed rounding and reset. */
static void test_output_smoothing(void)
{
    const int history[][2] = {{0, 0}, {-32768, -32768}, {32767, 32767}, {-32768, 32767}};
    for (int mod = 0; mod < 4; mod++)
        for (int trial = 0; trial < 4; trial++) {
            struct ob1_voice v, raw;
            struct ob1_knobs k = {63, 127, 8, 8, mod, 2};
            struct dsp_shape sh1, sh2;
            int32_t f1[32], s1[32], f2[32], s2[32], input[98], output[96];
            const int32_t *src[] = {f1, s1, f2, s2};
            const int32_t gain[] = {1295, 1295, 1295, 1295};
            const int32_t ng = ((DSP_MIX_NOISE_HIGH * DSP_NOISE_LEVEL) >> 12) * DSP_OUTPUT_GAIN >> 12;
            memset(&v, 0x55, sizeof v);
            ob1_voice_init(&v, 12345);
            ob1_voice_init(&raw, 12345);
            assert(v.smooth[0] == 0 && v.smooth[1] == 0);
            for (int i = 0; i < 2; i++) {
                v.smooth[i] = history[trial][i];
                input[1 - i] = history[trial][i] * 65536;
            }
            ob1_shape(&sh1, k.wav1);
            ob1_shape(&sh2, k.wav2);
            for (int block = 0; block < 3; block++) {
                ob1_voice_render(&v, &k, step(55), step(220), output + 32 * block);
                if (mod)
                    ob1_dsp_pair(&raw.vco1, &raw.vco2, step(55), step(220), &sh1, &sh2, mod,
                             f1, s1, f2, s2, 32);
                else {
                    ob1_dsp_osc_block(&raw.vco1, step(55), &sh1, f1, s1, 32);
                    ob1_dsp_osc_block(&raw.vco2, step(220), &sh2, f2, s2, 32);
                }
                ob1_dsp_voice_out(&raw.hpf, src, gain, &raw.noise, ng, input + 2 + 32 * block, 32);
                assert(!memcmp(&v.vco1, &raw.vco1, sizeof v.vco1));
                assert(!memcmp(&v.vco2, &raw.vco2, sizeof v.vco2));
                assert(v.noise == raw.noise && v.hpf.lp == raw.hpf.lp);
            }
            for (int i = 0; i < 96; i++) {
                double sum = input[i] / 65536.0 + 2 * (input[i + 1] / 65536.0) + input[i + 2] / 65536.0;
                assert(output[i] == (int32_t)(floor(sum / 4) * 65536));
            }
            ob1_voice_init(&v, 12345);
            ob1_voice_init(&raw, 12345);
            ob1_voice_render(&v, &k, step(55), step(220), output);
            ob1_voice_render(&raw, &k, step(55), step(220), input);
            assert(!memcmp(input, output, 32 * sizeof *input));
        }
}

/* Freeze baseline samples and every voice-state word through block-rate control changes. */
static void test_changing_controls_golden(void)
{
    const uint32_t expected[8] = {
        0xd7b86fffu, 0x4dff38aeu, 0xb76c4dc3u, 0x8af31e82u,
        0xa1cd0b87u, 0xdcbc87cau, 0x10763a7au, 0x5fcfe8ceu,
    };
    for (unsigned trial = 0; trial < 8; trial++) {
        struct ob1_voice v;
        uint32_t random = trial + 1, hash = 2166136261u;
        ob1_voice_init(&v, trial);
        for (int block = 0; block < 4096; block++) {
            uint32_t control[8], state[sizeof v / sizeof(uint32_t)];
            int32_t out[32];
            for (int i = 0; i < 8; i++)
                control[i] = random = random * 1664525u + 1013904223u;
            struct ob1_knobs k = {control[0] >> 25, control[1] >> 25,
                (control[2] >> 16) % 9, (control[3] >> 16) % 9,
                (block / 3 + trial) % 4, (block / 5 + trial) % 3};
            uint32_t inc1 = control[6] % (DSP_INC_MAX + 1u);
            uint32_t inc2 = control[7] % (DSP_INC_MAX + 1u);
            if (block % 17 == 0) inc1 = 0;
            if (block % 19 == 0) inc2 = DSP_INC_MAX;
            ob1_voice_render(&v, &k, inc1, inc2, out);
            for (int i = 0; i < 32; i++)
                hash = (hash ^ (uint32_t)out[i]) * 16777619u;
            memcpy(state, &v, sizeof v);
            for (unsigned i = 0; i < sizeof state / sizeof *state; i++)
                hash = (hash ^ state[i]) * 16777619u;
        }
        if (hash != expected[trial])
            fprintf(stderr, "changing controls %u: %08x, expected %08x\n", trial, hash, expected[trial]);
        assert(hash == expected[trial]);
    }
}

/* The fused loop must clip before smoothing, retain signed rounding and hold disabled noise. */
static void test_fused_output(void)
{
    int32_t source[4][32], actual[32], expected[32];
    const int32_t *src[] = {source[0], source[1], source[2], source[3]};
    unsigned clipped[2] = {0};
    uint32_t random = 1;
    for (int trial = 0; trial < 64; trial++) {
        struct dsp_hpf h = {(trial & 1) ? -8388608 : 8388608}, reference = h;
        uint32_t seed = (trial & 8) ? 0 : (uint32_t)trial, noise = seed, reference_noise = noise;
        int32_t smooth[2] = {-32768, 32767}, prev = smooth[0], older = smooth[1];
        int32_t gain[] = {4096, 4096, (trial & 2) ? 4096 : 0, (trial & 2) ? 4096 : 0};
        int32_t ng = (trial & 4) ? 512 : 0;
        for (int block = 0; block < 32; block++) {
            for (int i = 0; i < 32; i++)
                for (int j = 0; j < 4; j++) {
                    random = random * 1664525u + 1013904223u;
                    source[j][i] = block ? (int32_t)random >> 16 : (trial & 1) ? 32767 : -32768;
                }
            ob1_dsp_voice_out(&reference, src, gain, &reference_noise, ng, expected, 32);
            for (int i = 0; i < 32; i++) {
                clipped[0] += expected[i] == INT32_MIN;
                clipped[1] += expected[i] == INT32_MAX;
                int32_t x = expected[i] >> 16;
                expected[i] = ((x + 2 * prev + older) >> 2) * 65536;
                older = prev;
                prev = x;
            }
            voice_output(&h, src, gain, &noise, ng, actual, 32, smooth);
            assert(!memcmp(actual, expected, sizeof actual));
            assert(h.lp == reference.lp && noise == reference_noise);
            assert(smooth[0] == prev && smooth[1] == older);
            if (!ng) assert(noise == seed);
        }
    }
    assert(clipped[0] > 0 && clipped[1] > 0);
}

static void test_noise_output(void)
{
    struct ob1_voice v, split;
    struct dsp_hpf reference_hpf = {0};
    struct ob1_knobs k = {0};
    int32_t out[32], twice[32], raw[32], expected[32], zero[32] = {0};
    const int32_t *src[4] = {zero, zero, zero, zero};
    const int32_t *reference_src[4] = {raw, zero, zero, zero};
    const int32_t gain[4] = {0};
    const int32_t reference_gain[4] = {512, 0, 0, 0};
    uint32_t reference_noise = 12345, muted_noise = 1;
    int block;
    k.nois = 2;
    ob1_voice_init(&v, 12345);
    ob1_voice_init(&split, 12345);
    for (block = 0; block < 4096; block++) {
        ob1_dsp_voice_out(&v.hpf, src, gain, &v.noise, 512, out, 32);
        ob1_dsp_voice_out(&split.hpf, src, gain, &split.noise, 512, twice, 16);
        ob1_dsp_voice_out(&split.hpf, src, gain, &split.noise, 512, twice + 16, 16);
        ob1_dsp_noise(&reference_noise, raw, 32);
        ob1_dsp_voice_out(&reference_hpf, reference_src, reference_gain, &muted_noise, 0, expected, 32);
        assert(!memcmp(out, twice, sizeof out));
        /* Noise enters the same mixer unfiltered; shared output smoothing is checked separately. */
        assert(!memcmp(out, expected, sizeof out));
        assert(v.noise == reference_noise && split.noise == reference_noise);
    }
    assert(muted_noise == 1);
    ob1_dsp_voice_out(&v.hpf, src, gain, &v.noise, 0, out, 32);
    assert(v.noise == reference_noise);
    ob1_voice_render(&v, &k, 0, 0, out);
    ob1_dsp_noise(&reference_noise, raw, 32);
    assert(v.noise == reference_noise);
    printf("Noise: block continuity, unfiltered mixer routing and disabled state passed\n");
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
    ob1_dsp_voice_out(&h, src, high, &noise, 0, out, RATE);
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
    ob1_dsp_voice_out(&h, src, high, &noise, DSP_MIX_NOISE_HIGH, out, 64);
    for (i = 0; i < 64; i++)
        assert(out[i] == ((i & 1) ? INT32_MAX : INT32_MIN));
}

/* Pulse controls end at square; audible source shaping must not alter raw-ramp XMOD. */
static void test_wave_mapping_and_xmod_source(void)
{
    struct dsp_shape shape, carrier;
    int32_t f1[32], s1[32], f2[32], s2[32], reference[32 * 32];
    uint32_t previous = 0;
    int mode, wave, block, i;
    ob1_dsp_shape_saw(&carrier, 32768);
    for (wave = 64; wave < 128; wave++) {
        ob1_shape(&shape, wave);
        assert(shape.pulse && shape.width > previous && shape.width <= 0x80000000u);
        previous = shape.width;
    }
    ob1_shape(&shape, 64);
    assert(fabs(shape.width / 4294967296.0 - 0.067) < 1e-8);
    assert(fabs(previous / 4294967296.0 - 0.5) < 1e-7);
    for (mode = DSP_XMOD; mode <= (DSP_SYNC | DSP_XMOD); mode++)
        for (wave = 0; wave < 128; wave++) {
            struct dsp_osc a = {{0}, 0, 32767, 32767}, b = a;
            ob1_shape(&shape, wave);
            for (block = 0; block < 32; block++) {
                ob1_dsp_pair(&a, &b, step(200), step(2), &carrier, &shape, mode,
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
    test_waveform_golden();
    test_mix_levels();
    test_pitch_gain();
    test_output_smoothing();
    test_changing_controls_golden();
    test_fused_output();
    test_noise_output();
    test_headroom_and_additivity();
    test_mix_out();
    test_wave_mapping_and_xmod_source();
    test_modulation_routing();
    test_mixer_phase_continuity();
    puts("voice: all regressions passed");
    return 0;
}
