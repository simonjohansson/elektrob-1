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

int main(void)
{
    test_sine_points();
    test_sine_is_clean();
    test_fold_stays_in_range();
    test_render_scales_and_advances();
    puts("dsp: all tests passed");
    return 0;
}
