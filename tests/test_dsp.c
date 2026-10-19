/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Host tests for dsp/dsp.c: `mise run test`. */
#define _DEFAULT_SOURCE /* glibc hides M_PI under -std=c99 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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
static int32_t sub[RATE];       /* the sub-octave the renders also make */

static double render(int32_t g, int32_t *x)
{
    struct dsp_osc o = {{0}, 0, 32767, 32767};
    int32_t settle[RATE];
    double mean = 0;
    int i;
    ob1_dsp_saw_type(&o, step(F0), g, settle, sub, RATE);
    ob1_dsp_saw_type(&o, step(F0), g, x, sub, N);
    for (i = 0; i < N; i++)
        mean += x[i];
    return mean / N / 32768;
}

/* Frozen shift-loop normalization, including stale reciprocal state after a paired block. */
static void check_vco_setup(uint32_t inc)
{
    struct dsp_vco v = {0x12345678u, inc, 123u, 99u};
    uint32_t i = inc, shift = 0;
    while (i >= 1u << 16) { i >>= 1; shift++; }
    ob1_dsp_vco_set(&v, inc);
    assert(v.phase == 0x12345678u && v.inc == inc);
    assert(v.shift == shift && v.rinc == (i ? (1u << 30) / i : 0));
}

static void test_vco_setup(void)
{
    uint32_t shift, mantissa;
    /* Every normalized divisor and both ends of each discarded-bit range, through UINT32_MAX. */
    for (shift = 0; shift <= 16; shift++)
        for (mantissa = shift ? 32768 : 0; mantissa < 65536; mantissa++) {
            check_vco_setup(mantissa << shift);
            if (shift) check_vco_setup((mantissa << shift) | ((1u << shift) - 1));
        }
}

static void test_vco_resets(void)
{
    struct dsp_vco v = {0};
    uint32_t inc = step(1000);
    int i, resets = 0;
    ob1_dsp_vco_set(&v, inc);
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
        ob1_dsp_vco_set(&v, inc);
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
    struct dsp_osc o = {{0}, 0, 32767, 32767};
    int32_t settle[RATE];
    double mean = 0;
    int i;
    ob1_dsp_pulse_type(&o, step(F0), width, settle, sub, RATE);
    ob1_dsp_pulse_type(&o, step(F0), width, x, sub, N);
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

/* The sub-octave: a square at F0 / 2 (950 Hz, 95 bins, whole cycles in N), odd harmonics at 1/h less the
 * droop, no even ones and no DC; the same from either waveshape. */
static void test_sub_octave(void)
{
    int32_t x[N], s1[N];
    double mean = 0, x0;
    int i, k;
    render(32768, x);
    for (i = 0; i < N; i++) {
        s1[i] = sub[i];
        mean += sub[i];
    }
    assert(fabs(mean / N / 32768) < 0.005);
    spectrum(s1);
    x0 = M_PI * (F0 / 2) / RATE;
    for (k = 3; k <= 7; k += 2) {
        double d = pow(sin(k * x0) / (k * x0), 2) / pow(sin(x0) / x0, 2);
        assert(fabs(db(mag[k * F0 / 20]) - db(d / k)) < 0.3);
    }
    for (k = 2; k <= 6; k += 2)
        assert(db(mag[k * F0 / 20]) < -60);
    render_pulse(1u << 30, x);
    for (i = 0; i < N; i++)
        assert(abs(sub[i] - s1[i]) < 2);             /* same resets, same sub */
}

/* The flip-flop toggles once a reset: as many toggles as resets, each one at a reset. */
static void test_sub_follows_resets(void)
{
    struct dsp_osc o = {{0}, 0, 32767, 32767};
    int32_t x[RATE];
    int i, flips = 0;
    ob1_dsp_saw_type(&o, step(1000), 32768, x, sub, RATE);
    for (i = 1; i < RATE; i++)
        if ((sub[i] > 0) != (sub[i - 1] > 0))
            flips++;
    assert(abs(flips - (int)(48000.0 * step(1000) / 4294967296.0)) <= 1);
}

/* Sync: VCO2 at 1.5 F0, reset by VCO1 at F0, repeats at F0: its spectrum is F0's harmonics, the aliases
 * below 10 kHz far down. Unsynced, its own 2850 Hz is not one of them. */
static void test_sync(void)
{
    struct dsp_osc o1 = {{0}, 0, 32767, 32767}, o2 = {{0}, 0, 32767, 32767};
    struct dsp_shape saw;
    static int32_t f1[RATE], s1[RATE], f2[RATE], s2[RATE];
    double floor_sync;
    ob1_dsp_shape_saw(&saw, 32768);
    ob1_dsp_pair(&o1, &o2, step(F0), step(1.5 * F0), &saw, &saw, DSP_SYNC, f1, s1, f2, s2, RATE);
    ob1_dsp_pair(&o1, &o2, step(F0), step(1.5 * F0), &saw, &saw, DSP_SYNC, f1, s1, f2, s2, N);
    spectrum(f2);
    floor_sync = alias_floor(10000);
    printf("sync at %d Hz, VCO2 at 1.5x: aliases below 10 kHz %.1f dB\n", F0, floor_sync);
    assert(floor_sync < -40);
    ob1_dsp_pair(&o1, &o2, step(F0), step(1.5 * F0), &saw, &saw, 0, f1, s1, f2, s2, N);
    spectrum(f2);
    assert(alias_floor(10000) > -6);            /* 2850 Hz, between F0's harmonics */
}

/* A synced VCO2 slower than VCO1 never resets by itself: its sub-octave toggles once per VCO1 reset. */
static void test_sync_clocks_sub(void)
{
    struct dsp_osc o1 = {{0}, 0, 32767, 32767}, o2 = {{0}, 0, 32767, 32767};
    struct dsp_shape saw;
    static int32_t f1[RATE], s1[RATE], f2[RATE], s2[RATE];
    int i, flips = 0, resets = 0;
    ob1_dsp_shape_saw(&saw, 32768);
    ob1_dsp_pair(&o1, &o2, step(1000), step(750), &saw, &saw, DSP_SYNC, f1, s1, f2, s2, RATE);
    for (i = 1; i < RATE; i++) {
        flips += (s2[i] > 0) != (s2[i - 1] > 0);
        resets += (s1[i] > 0) != (s1[i - 1] > 0);  /* VCO1's sub flips at each VCO1 reset */
    }
    assert(abs(flips - resets) <= 1);
}

/* A synced VCO2 at a whole multiple of VCO1, at A4 (1200/11 samples a cycle, so where the sync falls in a
 * sample repeats every 11 cycles): its own reset meets the sync, and counts once, so its sub-octave flips 1
 * (unison) or 2 (an octave up) times a VCO1 reset, not a rhythm of extra flips. */
static void test_sync_whole_multiple(void)
{
    static int32_t f1[RATE], s1[RATE], f2[RATE], s2[RATE];
    struct dsp_shape saw;
    int m, i, flips, resets;
    ob1_dsp_shape_saw(&saw, 32768);
    for (m = 1; m <= 2; m++) {
        struct dsp_osc o1 = {{0}, 0, 32767, 32767}, o2 = {{0}, 0, 32767, 32767};
        ob1_dsp_pair(&o1, &o2, step(440), step(440) * m, &saw, &saw, DSP_SYNC, f1, s1, f2, s2, RATE);
        for (i = 1, flips = resets = 0; i < RATE; i++) {
            flips += (s2[i] > 0) != (s2[i - 1] > 0);
            resets += (s1[i] > 0) != (s1[i - 1] > 0);
        }
        assert(abs(flips - m * resets) <= m);
    }
}

/* The cross mod's factor is 2^((DSP_XMOD_DEPTH_Q6 / 64) x / 32768) within 0.1 %, and saturates at max. */
static void test_xmod_scale(void)
{
    int32_t x;
    for (x = -32768; x < 32768; x += 97)
        assert(fabs(ob1_dsp_xmod_scale(65535, x, -14, 0xffffffffu) / (65535 / 16384.0 * 16384 *
                    pow(2, (DSP_XMOD_DEPTH_Q6 / 64.0) * (double)x / 32768)) - 1) < 0.001);
    assert(ob1_dsp_xmod_scale(65535, 32767, 0, DSP_INC_MAX) == DSP_INC_MAX);
}

/* OSC 2 SAW rises into the same pitch summer as KEY CV, so it must raise VCO1's rate. */
static void test_xmod_circuit_direction(void)
{
    static const double notes[] = {55, 220, 880};
    struct dsp_shape saw;
    int note, phase;
    ob1_dsp_shape_saw(&saw, 32768);
    for (note = 0; note < 3; note++) {
        uint32_t previous = 0;
        for (phase = 0; phase < 8; phase++) {
            struct dsp_osc a = {{0}, 0, 32767, 32767}, b = a;
            int32_t f1, s1, f2, s2;
            b.vco.phase = (uint32_t)phase << 29;
            ob1_dsp_pair(&a, &b, step(notes[note]), step(notes[note]), &saw, &saw,
                     DSP_SYNC | DSP_XMOD, &f1, &s1, &f2, &s2, 1);
            assert(a.vco.phase > previous);
            if (!phase) assert(a.vco.phase < step(notes[note]));
            if (phase == 7) assert(a.vco.phase > step(notes[note]));
            previous = a.vco.phase;
        }
    }
}

/* Sync injects reset events; changing master pitch must not rescale the slave between them. */
static void test_sync_circuit_timing(void)
{
    static const double notes[] = {55, 220, 880};
    static const double ratios[] = {1, 1.4983070768766815, 2, 4};
    struct dsp_shape saw;
    int note, ratio, mode, i;
    ob1_dsp_shape_saw(&saw, 32768);
    for (note = 0; note < 3; note++)
        for (ratio = 0; ratio < 4; ratio++)
            for (mode = DSP_SYNC; mode <= (DSP_SYNC | DSP_XMOD); mode++) {
                struct dsp_osc a = {{0}, 0, 32767, 32767}, b = a;
                uint32_t slave_inc = step(notes[note] * ratios[ratio]);
                int forced = 0, free_steps = 0;
                a.vco.phase = 0x12345678u;
                b.vco.phase = 0x98765432u;
                for (i = 0; i < 4800; i++) {
                    uint32_t old_master = a.vco.phase, old_slave = b.vco.phase;
                    int32_t f1, s1, f2, s2;
                    uint32_t master_inc = step(notes[note] * ((i / 32) & 1 ? 1.5 : 1));
                    ob1_dsp_pair(&a, &b, master_inc, slave_inc, &saw, &saw, mode,
                             &f1, &s1, &f2, &s2, 1);
                    if ((mode & DSP_SYNC) && a.vco.phase < old_master) {
                        uint32_t advance = a.vco.phase - old_master;
                        double expected = (double)slave_inc * a.vco.phase / advance;
                        /* XMOD truncates 14 base bits before scaling; its reciprocal uses the full base. */
                        double base_rounding = (mode & DSP_XMOD) ? (master_inc & 16383u) / (double)master_inc : 0;
                        double sample_error = 1.0 / 1024 + base_rounding;
                        assert(fabs((double)b.vco.phase - expected) <= slave_inc * sample_error + 2);
                        forced++;
                    } else {
                        assert(b.vco.phase == old_slave + slave_inc);
                        free_steps++;
                    }
                }
                assert(free_steps > 0);
                assert((mode & DSP_SYNC) ? forced > 0 : forced == 0);
            }
}

/* The reference's slow-ramp full-depth mean reset ratio is 2.78; test without the pitch clamp. */
static void test_xmod_pitch(void)
{
    struct dsp_osc o1 = {{0}, 0, 32767, 32767}, o2 = {{0}, 0, 32767, 32767};
    struct dsp_shape saw;
    static int32_t f1[RATE], s1[RATE], f2[RATE], s2[RATE];
    int i, flips = 0;
    ob1_dsp_shape_saw(&saw, 32768);
    ob1_dsp_pair(&o1, &o2, step(200), step(2), &saw, &saw, DSP_XMOD, f1, s1, f2, s2, RATE);
    for (i = 1; i < RATE; i++)
        flips += (s1[i] > 0) != (s1[i - 1] > 0);    /* VCO1's resets */
    printf("xmod: VCO1 at %d resets a second, against 556\n", flips);
    assert(fabs(flips / 556.0 - 1) < 0.015);
}

/* Cross mod at a high pitch: VCO1's peak (2 kHz x 15.16) sits at DSP_INC_MAX for part of VCO2's ramp, where
 * its reciprocal follows the clamped step, so the band-limited output stays within a polyBLEP's overshoot. */
static void test_xmod_clamp(void)
{
    struct dsp_osc o1 = {{0}, 0, 32767, 32767}, o2 = {{0}, 0, 32767, 32767};
    struct dsp_shape saw;
    static int32_t f1[RATE / 10], s1[RATE / 10], f2[RATE / 10], s2[RATE / 10];
    int i;
    ob1_dsp_shape_saw(&saw, 32768);
    ob1_dsp_pair(&o1, &o2, step(2000), step(50), &saw, &saw, DSP_XMOD, f1, s1, f2, s2, RATE / 10);
    for (i = 0; i < RATE / 10; i++)
        assert(abs(f1[i]) < 49152 && abs(s1[i]) < 49152);
}

/* Noise: no DC, the RMS of a uniform spread (1 / sqrt 3), and white: neighbouring samples uncorrelated. */
static void test_noise(void)
{
    static int32_t x[RATE];
    uint32_t state = 1;
    double mean = 0, power = 0, corr = 0;
    int i;
    ob1_dsp_noise(&state, x, RATE);
    for (i = 0; i < RATE; i++) {
        mean += x[i] / 32768.0;
        power += pow(x[i] / 32768.0, 2);
        if (i)
            corr += (x[i] / 32768.0) * (x[i - 1] / 32768.0);
    }
    assert(fabs(mean / RATE) < 0.01);
    assert(fabs(sqrt(power / RATE) - 1 / sqrt(3)) < 0.01);
    assert(fabs(corr / power) < 0.02);
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
    ob1_dsp_vco_set(&v, step(F0));
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

/* Immediate reset clocks one sub event per natural/forced reset, merging coincident events. */
static void test_sync_ratio_boundaries(void)
{
    const uint32_t inc = 1u << 25;  /* 128 samples per master period: exact event positions. */
    int multiple, side, shape, i, resets;
    int32_t f1, s1, f2, s2;
    struct dsp_shape saw, square;
    ob1_dsp_shape_saw(&saw, 32768);
    ob1_dsp_shape_pulse(&square, 0x80000000u);
    for (multiple = 1; multiple <= 2; multiple++)
        for (side = -1; side <= 1; side++)
            for (shape = 0; shape < 2; shape++) {
                struct dsp_osc a = {{0}, 0, 32767, 32767}, b = a;
                int32_t previous1 = a.sub;
                double energy = 0;
                resets = 0;
                for (i = 0; i < 128 * 200; i++) {
                    ob1_dsp_pair(&a, &b, inc, inc * multiple + side * (int32_t)(inc / 512),
                             &saw, shape ? &square : &saw, DSP_SYNC, &f1, &s1, &f2, &s2, 1);
                    resets += a.sub != previous1;
                    previous1 = a.sub;
                    if (i % 128 == 127)
                        assert(b.sub == ((resets * (multiple + (side > 0))) & 1 ? -32768 : 32767));
                    energy += (double)f2 * f2;
                }
                assert(sqrt(energy / i) > 8000);  /* A synced square retains both comparator edges. */
            }
}

/* At an exact sample boundary a full positive pulse jump must subtract, never wrap sign. */
static void test_blep_full_jump(void)
{
    int32_t prev = 0, cur = 32767;
    ob1_dsp_blep(&prev, &cur, 0, 65536, 0);
    assert(prev == 0 && cur == -1);
    prev = 0;
    cur = -32768;
    ob1_dsp_blep(&prev, &cur, 0, -65536, 0);
    assert(prev == 0 && cur == 0);
}

/* Standalone and paired full-saw evaluators must agree sample for sample. */
static void test_full_saw_fast_path(void)
{
    const uint32_t increments[] = {1u << 15, 19685266u, 1u << 28, DSP_INC_MAX};
    unsigned k;
    for (k = 0; k < sizeof(increments) / sizeof(increments[0]); k++) {
        struct dsp_osc a = {{0}, 0, 32767, 32767}, b = a;
        struct dsp_shape saw;
        struct dsp_osc other_vco = a;
        int32_t fast[32], fs[32], generic[32], gs[32], other[32], os[32];
        int block, i;
        ob1_dsp_shape_saw(&saw, 32768);
        for (block = 0; block < 100; block++) {
            ob1_dsp_osc_block(&a, increments[k], &saw, fast, fs, 32);
            ob1_dsp_pair(&b, &other_vco, increments[k], increments[k], &saw, &saw,
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
    ob1_dsp_shape_saw(&carrier, 32768);
    for (mod = 0; mod < 4; mod++)
        for (shape = 0; shape < 3; shape++) {
            struct dsp_osc a = {{0}, 0, 32767, 32767}, b = a;
            int32_t output[4][32];
            uint32_t hash = 2166136261u;
            a.vco.phase = 0x12345678u; b.vco.phase = 0x98765432u;
            if (!shape) ob1_dsp_shape_saw(&source, -12343);
            else ob1_dsp_shape_pulse(&source, shape == 1 ? 0x80000000u : 287762809u);
            for (block = 0; block < 100; block++) {
                ob1_dsp_pair(&a, &b, increments[shape][0], increments[shape][1], &carrier, &source,
                         mod, output[0], output[1], output[2], output[3], 32);
                for (i = 0; i < 32; i++)
                    for (j = 0; j < 4; j++)
                        hash = (hash ^ (uint32_t)output[j][i]) * 16777619u;
            }
            assert(hash == expected[mod][shape]);
        }
}

/* Fixed full-saw slave captures include changing pitch, routing and retained oscillator history. */
static void test_full_saw_slave_golden(void)
{
    static const uint32_t increments[] = {0, 1, 32768, 65535, 65536, 19685266,
                                         29494959, 157482134, DSP_INC_MAX, DSP_INC_MAX + 1};
    static const uint32_t expected[4][4] = {
        {0xcb3101feu, 0x52b6fa2au, 0x6c34f853u, 0xbc0f76e5u},
        {0x0f732f97u, 0x59ead8a7u, 0x3f6f9e86u, 0x1b881d5cu},
        {0x0802baa1u, 0x7e79e16eu, 0xe04768a9u, 0x84407f4bu},
        {0xe21b22d4u, 0x456e7e8cu, 0x1d4ccfafu, 0x5ad591c7u},
    };
    struct dsp_shape carrier, source;
    int mod, shape, block, i, j;
    ob1_dsp_shape_saw(&source, 32768);
    for (mod = 0; mod < 4; mod++)
        for (shape = 0; shape < 4; shape++) {
            struct dsp_osc a = {{0}, 0, 32767, 32767}, b = a;
            int32_t output[4][32];
            uint32_t hash = 2166136261u;
            a.vco.phase = 0x12345678u; b.vco.phase = 0x98765432u;
            if (shape < 2) ob1_dsp_shape_saw(&carrier, shape ? -12343 : 32768);
            else ob1_dsp_shape_pulse(&carrier, shape == 2 ? 0x80000000u : 287762809u);
            for (block = 0; block < 400; block++) {
                uint32_t inc1 = increments[(block / 7) % 10];
                uint32_t inc2 = block % 3 ? inc1 : increments[(block / 11 + 3) % 10];
                ob1_dsp_pair(&a, &b, inc1, inc2, &carrier, &source, mod,
                             output[0], output[1], output[2], output[3], 32);
                for (i = 0; i < 32; i++)
                    for (j = 0; j < 4; j++)
                        hash = (hash ^ (uint32_t)output[j][i]) * 16777619u;
                const struct dsp_osc *osc[2] = {&a, &b};
                for (j = 0; j < 2; j++) {
                    const struct dsp_osc *o = osc[j];
                    uint32_t state[] = {o->vco.phase, o->vco.inc, o->vco.rinc, o->vco.shift,
                                        (uint32_t)o->prev, (uint32_t)o->sub, (uint32_t)o->sub_prev};
                    for (i = 0; i < 7; i++)
                        hash = (hash ^ state[i]) * 16777619u;
                }
            }
            assert(hash == expected[mod][shape]);
        }
}

int main(void)
{
    test_full_saw_slave_golden();
    test_full_saw_fast_path();
    test_full_saw_pair_golden();
    test_blep_full_jump();
    test_vco_setup();
    test_vco_resets();
    test_vco_crossing_position();
    test_saw();
    test_saw_type_saw();
    test_saw_type_triangle();
    test_saw_type_half();
    test_saw_type_aliasing();
    test_pulse_type_square();
    test_pulse_type_narrow();
    test_sub_octave();
    test_sub_follows_resets();
    test_sync();
    test_sync_clocks_sub();
    test_sync_whole_multiple();
    test_sync_ratio_boundaries();
    test_xmod_scale();
    test_xmod_circuit_direction();
    test_sync_circuit_timing();
    test_xmod_pitch();
    test_xmod_clamp();
    test_noise();
    puts("dsp: all tests passed");
    return 0;
}
