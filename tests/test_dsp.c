/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Host tests for machine/dsp.c: `mise run test`. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "dsp.h"

#define RATE 48000
#define N 4800                  /* 0.1 s: 10 Hz bins */
#define F0 1900                 /* 190 bins, 190 whole cycles; its aliases fall between its harmonics */

static double mag[N / 2];

/* |DFT| of x in each 10 Hz bin, normalised to the largest. */
static void spectrum(const int32_t *x)
{
    double peak = 0;
    int k, i;
    for (k = 1; k < N / 2; k++) {
        double re = 0, im = 0;
        for (i = 0; i < N; i++) {
            re += x[i] * cos(2 * M_PI * k * i / N);
            im += x[i] * sin(2 * M_PI * k * i / N);
        }
        mag[k] = sqrt(re * re + im * im);
        if (mag[k] > peak)
            peak = mag[k];
    }
    for (k = 1; k < N / 2; k++)
        mag[k] /= peak;
}

static double db(double v) { return 20 * log10(v > 1e-12 ? v : 1e-12); }
static double harmonic(int h) { return db(mag[h * F0 / 10]); }

/* The strongest bin below `hz` that is not a harmonic of F0, in dB below the strongest. */
static double alias_floor(int hz)
{
    double worst = 0;
    int k;
    for (k = 1; k < hz / 10; k++)
        if (k % (F0 / 10) && mag[k] > worst)
            worst = mag[k];
    return db(worst);
}

static uint32_t step(double f) { return (uint32_t)(f / RATE * 4294967296.0); }

/* N samples of saw type g after a second to settle; the mean too. */
static double render(int32_t g, int32_t *x)
{
    struct dsp_osc o = {{0}, 0};
    int32_t settle[RATE];
    double mean = 0;
    int i;
    dsp_saw_type(&o, step(F0), g, settle, RATE);
    dsp_saw_type(&o, step(F0), g, x, N);
    for (i = 0; i < N; i++)
        mean += x[i];
    return mean / N / 32768;
}

static void test_vco_resets(void)
{
    struct dsp_vco v = {0};
    uint32_t inc = step(1000);
    int i, resets = 0;
    dsp_vco_set(&v, inc);
    for (i = 0; i < RATE; i++) {
        int32_t t = dsp_vco_step(&v);
        if (t >= 0) {
            assert(fabs(t - (double)v.phase / inc * 65536) < 65536 / 1024.0);
            resets++;
        }
    }
    assert(resets == (int)(48000.0 * inc / 4294967296.0));   /* 999: the step is rounded down */
}

/* The position of a crossing stays within 1/1024 of a sample from 1 Hz to 20 kHz, at any phase. */
static void test_vco_crossing_position(void)
{
    double f;
    uint32_t seed = 1;
    for (f = 1; f < 20000; f *= 1.37) {
        struct dsp_vco v = {0};
        uint32_t inc = step(f);
        int i;
        dsp_vco_set(&v, inc);
        for (i = 0; i < 1000; i++) {
            int32_t t;
            seed = seed * 1664525u + 1013904223u;
            v.phase = 0x80000000u - 1 - seed % inc;  /* the next step passes the middle */
            dsp_vco_step(&v);
            t = dsp_vco_crossed(&v, 0x80000000u);
            assert(t >= 0);
            assert(fabs(t - (double)(v.phase - 0x80000000u) / inc * 65536) < 65536 / 1024.0);
        }
    }
}

static void test_saw(void)
{
    assert(dsp_saw(0) == -32768);
    assert(dsp_saw(1u << 31) == 0);
    assert(dsp_saw(0xffffffffu) == 32767);
}

/* The 2-point polyBLEP's two-sample kernel rounds off the top like sinc^2: -0.7 dB at 7.6 kHz. */
static double droop(int h)
{
    double x = M_PI * h * F0 / RATE;
    return pow(sin(x) / x, 2);
}

/* g = +1: a saw, every harmonic at 1/h, less the droop. */
static void test_saw_type_saw(void)
{
    int32_t x[N];
    int h;
    assert(fabs(render(32768, x)) < 0.005);
    spectrum(x);
    for (h = 2; h <= 6; h++)
        assert(fabs(harmonic(h) - db(droop(h) / h)) < 0.3);
}

/* g = -1: a triangle, odd harmonics at 1/h^2, no even ones. */
static void test_saw_type_triangle(void)
{
    int32_t x[N];
    assert(fabs(render(-32768, x)) < 0.005);
    spectrum(x);
    assert(fabs(harmonic(3) - db(droop(3) / 9)) < 0.3);
    assert(fabs(harmonic(5) - db(droop(5) / 25)) < 0.5);
    assert(harmonic(2) < -60 && harmonic(4) < -60);
}

/* Halfway, g = 0: the saw's upper half only, less its mean. */
static void test_saw_type_half(void)
{
    int32_t x[N];
    assert(fabs(render(0, x)) < 0.005);
}

/* N samples of pulse type `width` after a second to settle; the mean too. */
static double render_pulse(uint32_t width, int32_t *x)
{
    struct dsp_osc o = {{0}, 0};
    int32_t settle[RATE];
    double mean = 0;
    int i;
    dsp_pulse_type(&o, step(F0), width, settle, RATE);
    dsp_pulse_type(&o, step(F0), width, x, N);
    for (i = 0; i < N; i++)
        mean += x[i];
    return mean / N / 32768;
}

/* The square: odd harmonics at 1/h (less the droop), no even ones, no DC. */
static void test_pulse_type_square(void)
{
    int32_t x[N];
    assert(fabs(render_pulse(1u << 31, x)) < 0.005);
    spectrum(x);
    assert(fabs(harmonic(3) - db(droop(3) / 3)) < 0.3);
    assert(fabs(harmonic(5) - db(droop(5) / 5)) < 0.3);
    assert(harmonic(2) < -60 && harmonic(4) < -60);
}

/* A 10 % pulse: harmonic h at sin(pi h d) / h against the first, and no DC. */
static void test_pulse_type_narrow(void)
{
    int32_t x[N];
    double d = 0.1;
    int h;
    assert(fabs(render_pulse((uint32_t)(d * 4294967296.0), x)) < 0.005);
    spectrum(x);
    for (h = 2; h <= 6; h++)
        assert(fabs(harmonic(h) - db(droop(h) * sin(M_PI * h * d) / h / sin(M_PI * d))) < 0.5);
}

/* The band-limiting, at 1.9 kHz. The 2-point polyBLEP is weak just above Nyquist (the 13th harmonic,
 * 24.7 kHz, folds to 23.3 kHz at -31 dB), so the check is on the aliases that land below 10 kHz. */
static void test_saw_type_aliasing(void)
{
    int32_t x[N];
    struct dsp_vco v = {0};
    double blep, blep_all, naive, naive_all;
    int i;
    render(32768, x);
    spectrum(x);
    blep = alias_floor(10000);
    blep_all = alias_floor(RATE / 2);
    dsp_vco_set(&v, step(F0));
    for (i = 0; i < N; i++) {
        dsp_vco_step(&v);
        x[i] = dsp_saw(v.phase);
    }
    spectrum(x);
    naive = alias_floor(10000);
    naive_all = alias_floor(RATE / 2);
    printf("saw at %d Hz, aliases below 10 kHz: %.1f dB (naive %.1f); anywhere: %.1f dB (naive %.1f)\n",
           F0, blep, naive, blep_all, naive_all);
    assert(blep < naive - 20);
}

int main(void)
{
    test_vco_resets();
    test_vco_crossing_position();
    test_saw();
    test_saw_type_saw();
    test_saw_type_triangle();
    test_saw_type_half();
    test_saw_type_aliasing();
    test_pulse_type_square();
    test_pulse_type_narrow();
    puts("dsp: all tests passed");
    return 0;
}
