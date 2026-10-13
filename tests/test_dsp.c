/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Host tests for machine/dsp.c: `mise run test`. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "dsp.h"

#define N 4096

/* Total harmonic distortion of one cycle (harmonics 2-15) against the fundamental, by DFT. */
static double thd(const double *x)
{
    double h1 = 0, rest = 0;
    int k, i;
    for (k = 1; k < 16; k++) {
        double re = 0, im = 0;
        for (i = 0; i < N; i++) {
            re += x[i] * cos(2 * M_PI * k * i / N);
            im += x[i] * sin(2 * M_PI * k * i / N);
        }
        if (k == 1)
            h1 = re * re + im * im;
        else
            rest += re * re + im * im;
    }
    return sqrt(rest / h1);
}

static void test_sine_points(void)
{
    assert(dsp_sine(0) == 0);
    assert(dsp_sine(1u << 30) >= 32700);                   /* a quarter cycle: the peak */
    assert(dsp_sine(3u << 30) <= -32700);
    assert(dsp_sine(1u << 31) == 0);
}

static void test_sine_is_clean(void)
{
    double x[N];
    int i;
    for (i = 0; i < N; i++)
        x[i] = dsp_sine((uint32_t)i << 20);
    assert(thd(x) < 0.001);                                /* 0.1 % */
}

static void test_fold_stays_in_range(void)
{
    int32_t shape, v;
    uint32_t ph;
    for (shape = 0; shape < 128; shape++)
        for (ph = 0; ph < (1u << 31); ph += 1u << 20) {
            v = dsp_fold(dsp_sine(ph), shape);
            assert(v >= -32767 && v <= 32767);
        }
}

static void test_render_scales_and_advances(void)
{
    int32_t out[32];
    uint32_t phase = 1u << 30;                             /* at the peak */
    dsp_render(&phase, 0, 0, 1 << 14, out, 32);            /* gain 1.0 */
    assert(out[0] >= (int32_t)(0.998 * 2147483647.0));     /* full scale, no wrap */
    assert(phase == 1u << 30);
    dsp_render(&phase, 1000, 0, 1 << 14, out, 32);
    assert(phase == (1u << 30) + 32000);
}

/* A ~1 kHz ramp resets once every 2^32 of phase, and every reset's position matches the exact one. */
static void test_vco_resets(void)
{
    struct dsp_vco v = {0};
    uint32_t inc = (uint32_t)(1000.0 / 48000 * 4294967296.0);
    int i, resets = 0;
    dsp_vco_set(&v, inc);
    for (i = 0; i < 48000; i++) {
        int32_t t = dsp_vco_step(&v);
        if (t >= 0) {
            double exact = (double)v.phase / inc * 65536;
            assert(fabs(t - exact) < 65536 / 1024.0);  /* within 1/1024 of a sample */
            resets++;
        }
    }
    assert(resets == (int)(48000.0 * inc / 4294967296.0));   /* 999: the step is rounded down */
}

/* The reset's position stays accurate from 1 Hz to 20 kHz, at any phase. */
static void test_vco_reset_position(void)
{
    double f;
    uint32_t seed = 1;
    for (f = 1; f < 20000; f *= 1.37) {
        struct dsp_vco v = {0};
        uint32_t inc = (uint32_t)(f / 48000 * 4294967296.0);
        int i;
        dsp_vco_set(&v, inc);
        for (i = 0; i < 1000; i++) {
            int32_t t;
            seed = seed * 1664525u + 1013904223u;
            v.phase = (uint32_t)0 - 1 - seed % inc;    /* the next step resets */
            t = dsp_vco_step(&v);
            assert(t >= 0);
            assert(fabs(t - (double)v.phase / inc * 65536) < 65536 / 1024.0);
        }
    }
}

static void test_saw(void)
{
    assert(dsp_saw(0) == -32768);
    assert(dsp_saw(1u << 31) == 0);
    assert(dsp_saw(0xffffffffu) == 32767);
}

int main(void)
{
    test_sine_points();
    test_sine_is_clean();
    test_fold_stays_in_range();
    test_render_scales_and_advances();
    test_vco_resets();
    test_vco_reset_position();
    test_saw();
    puts("dsp: all tests passed");
    return 0;
}
