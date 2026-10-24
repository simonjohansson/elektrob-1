#!/usr/bin/env python3
"""Host harness: the OB-1 Digitone adapter's render through a stubbed companion.

Compiles machines/digitone/engine.c + dsp/dsp.c + dsp/voice.c +
machines/digitakt/knobs.c natively with stubbed dnm_phase_inc and fw_*
arrays.

Two parts (the VCOs run freely, so exact A/B waveform comparisons only
work across identical start phases):

- in-process: pitch, FRQ/FIN, the sub octave, loudness, fuse, cross-track,
  reap, extremes and descriptor checks (phase-independent assertions);
- fresh-process pairs: a CLI driver renders one knob setup per process
  (identical init every time), and Python compares the bytes: wave
  families, sync, cross-mod, LFO routing and silence.

The knob order is the adapter's: SYN1 A-H (FND1 OCT1 XMOD -- FND2 OCT2
SYNC NOIS) then SYN2 A-H (FRQ1 FIN1 TYP1 WAV1 FRQ2 FIN2 TYP2 WAV2).

Must stay green.
"""
import pathlib
import struct
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
INC = ROOT / "vendor" / "elekloader" / "elekloader" / "sdk" / "include"

COMMON = r"""
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "digitone-mk1/machines.h"
#include "voice.h"
#include "knobs.h"

#define DNM_PEAK 0x08000000

volatile int16_t fw_voice_params[1024];
volatile int32_t fw_lfo_state[256];
volatile uint32_t fw_voice_pitch[8];
volatile int32_t fw_voice_len[8];
volatile uint32_t fw_gate_off;
volatile uint32_t fw_timeline;
volatile int32_t fw_transpose[5];
volatile int32_t fw_voice_track[8];
volatile uint32_t fw_gate_on;
const char fw_str_empty[] = "";

uint32_t dnm_phase_inc(int32_t pitch) {
    int32_t n = pitch >> 16;
    uint32_t f = (uint32_t)pitch & 0xffffu;
    double fa, fb;
    uint32_t a, b;
    if (n < 0) n = 0;
    if (n > 127) n = 127;
    fa = 440.0 * pow(2.0, ((double)n - 69.0) / 12.0) / 48000.0 * 4294967296.0;
    fb = 440.0 * pow(2.0, ((double)(n + 1) - 69.0) / 12.0) / 48000.0 * 4294967296.0;
    a = (uint32_t)(fa + 0.5); b = (uint32_t)(fb + 0.5);
    return a + (uint32_t)(((b - a) >> 8) * (f >> 8));
}

extern const struct dnm_machine ob1_machine;
extern uint32_t ob1_ticks;
void ob1_reap(void);
void ob1_voice_on(int32_t voice, int32_t track, void *event);
void ob1_block(int32_t *voices);

static const int slot_of[16] = {
    17, 18, 19, 20, 21, 22, 23, 24, 27, 28, 29, 30, 31, 32, 33, 34,
};
static void set_slot(int v, int slot, int32_t val) {
    fw_voice_params[9 + 79 * v + slot] = (int16_t)val;
}
static void set_knob(int v, int i, int32_t val) { set_slot(v, slot_of[i], val); }
"""

DRIVER = COMMON + r"""
/* The track that switches to OB-1: both VCOs saws in unison, VCO1's FUND
 * ON, VCO2's at -3 dB, no mod, noise off (the adapter's own defaults). */
static void defaults(int v) {
    set_knob(v, 0, 6 * 256);    /* FND1 ON */
    set_knob(v, 1, 0);          /* OCT1 OFF */
    set_knob(v, 2, 0);          /* XMOD OFF */
    set_knob(v, 3, 0);          /* (blank) */
    set_knob(v, 4, 63 * 256);   /* FND2 -3DB */
    set_knob(v, 5, 0);          /* OCT2 OFF */
    set_knob(v, 6, 0);          /* SYNC OFF */
    set_knob(v, 7, 0);          /* NOIS OFF */
    set_knob(v, 8, 64 * 256);   /* FRQ1 0 */
    set_knob(v, 9, 64 * 256);   /* FIN1 0 */
    set_knob(v, 10, 0);         /* TYP1 saw */
    set_knob(v, 11, 127 * 256); /* WAV1 full saw */
    set_knob(v, 12, 64 * 256);  /* FRQ2 0 */
    set_knob(v, 13, 64 * 256);  /* FIN2 0 */
    set_knob(v, 14, 0);         /* TYP2 saw */
    set_knob(v, 15, 127 * 256); /* WAV2 full saw */
    fw_lfo_state[20 * v + 16] = 127;
    fw_lfo_state[20 * v + 17] = 0;
    fw_lfo_state[20 * v + 10 + 16] = 127;
    fw_lfo_state[20 * v + 10 + 17] = 0;
}
static void run(int v, struct dnm_voice *dv, int32_t *out, int nblocks) {
    int b;
    for (b = 0; b < nblocks; b++) {
        if (b == 0) {
            dv->fresh = 1;
            ob1_voice_on(v, dv->track, 0);
        }
        fw_gate_off = 0;
        ob1_block(0);
        ob1_machine.render(v, out + 32 * b, dv);
        dv->fresh = 0;
    }
}
static int32_t peak32(const int32_t *x, int n) {
    int32_t m = 0, i;
    for (i = 0; i < n; i++) {
        int32_t a = x[i] < 0 ? -x[i] : x[i];
        if (a > m) m = a;
    }
    return m;
}
static double zc_rate(const int32_t *x, int n) {
    int c = 0, i;
    for (i = 0; i + 1 < n; i++)
        if ((x[i] < 0) != (x[i+1] < 0)) c++;
    return (double)c / (double)(n - 1);
}
/* Mean samples between rising zero crossings: precise enough for the
 * +-50 cent fine tune where a whole-window crossing count is too coarse. */
static double period_samples(const int32_t *x, int n) {
    int i, prev = -1, cnt = 0;
    long sum = 0;
    for (i = 1; i < n; i++) {
        if (x[i-1] < 0 && x[i] >= 0) {
            if (prev >= 0) { sum += i - prev; cnt++; }
            prev = i;
        }
    }
    return cnt ? (double)sum / (double)cnt : 0.0;
}

static int fails = 0;
#define CHECK(name, cond) do { \
    printf("%s %s\n", (cond) ? "PASS" : "FAIL", name); \
    if (!(cond)) fails++; } while (0)

int main(void) {
    static int32_t buf[32 * 64], buf2[32 * 64];
    struct dnm_voice dv;
    double zc, expect;

    memset(&dv, 0, sizeof dv);
    dv.velocity = 100; dv.track = 0; dv.gate = 1;
    defaults(0);
    dv.pitch = 69 << 16;

    /* 1. Pitch: two saws in unison at A4 ~440 Hz. */
    run(0, &dv, buf, 48);
    zc = zc_rate(buf + 8 * 32, 40 * 32);
    expect = 2.0 * 440.0 / 48000.0;
    printf("info zc-a4 %f expect %f peak %d\n", zc, expect, peak32(buf, 48 * 32));
    CHECK("pitch-a4", zc > expect * 0.95 && zc < expect * 1.05);

    /* 2. FRQ1: VCO1 alone, +12 semitones doubles its rate. */
    set_knob(0, 4, 0);          /* FND2 OFF: VCO1 alone */
    run(0, &dv, buf, 48);
    zc = zc_rate(buf + 8 * 32, 40 * 32);
    set_knob(0, 8, 88 * 256);   /* FRQ1 +12 */
    run(0, &dv, buf2, 48);
    {
        double z2 = zc_rate(buf2 + 8 * 32, 40 * 32);
        printf("info frq1 %f -> %f\n", zc, z2);
        CHECK("frq1-octave", z2 > zc * 1.9 && z2 < zc * 2.1);
    }
    set_knob(0, 8, 64 * 256);

    /* 3. FIN1: VCO1 fine tune about +-50 cents around the note. */
    run(0, &dv, buf, 48);
    zc = period_samples(buf + 8 * 32, 40 * 32);
    set_knob(0, 9, 127 * 256);
    run(0, &dv, buf2, 48);
    {
        double p2 = period_samples(buf2 + 8 * 32, 40 * 32);
        printf("info fin1 centre %f up %f ratio %f\n", zc, p2, zc / p2);
        CHECK("fin1-up", zc / p2 > 1.02);
    }
    set_knob(0, 9, 0);
    run(0, &dv, buf2, 48);
    {
        double p2 = period_samples(buf2 + 8 * 32, 40 * 32);
        printf("info fin1 centre %f down %f ratio %f\n", zc, p2, zc / p2);
        CHECK("fin1-down", zc / p2 < 0.98);
    }
    set_knob(0, 9, 64 * 256);

    /* 4. FRQ2: VCO2 alone, +12 semitones doubles its rate. */
    set_knob(0, 0, 0);          /* FND1 OFF */
    set_knob(0, 4, 89 * 256);   /* FND2 ON */
    run(0, &dv, buf, 48);
    zc = zc_rate(buf + 8 * 32, 40 * 32);
    set_knob(0, 12, 88 * 256);  /* FRQ2 +12 */
    run(0, &dv, buf2, 48);
    {
        double z2 = zc_rate(buf2 + 8 * 32, 40 * 32);
        printf("info frq2 %f -> %f\n", zc, z2);
        CHECK("frq2-octave", z2 > zc * 1.9 && z2 < zc * 2.1);
    }
    set_knob(0, 12, 64 * 256);

    /* 5. FIN2: VCO2 fine tune about +-50 cents. */
    run(0, &dv, buf, 48);
    zc = period_samples(buf + 8 * 32, 40 * 32);
    set_knob(0, 13, 127 * 256);
    run(0, &dv, buf2, 48);
    {
        double p2 = period_samples(buf2 + 8 * 32, 40 * 32);
        printf("info fin2 centre %f up %f ratio %f\n", zc, p2, zc / p2);
        CHECK("fin2-up", zc / p2 > 1.02);
    }
    set_knob(0, 13, 64 * 256);

    /* 6. OCT1: VCO1's sub octave alone sounds an octave below its fund. */
    set_knob(0, 4, 0);          /* VCO2 OFF */
    set_knob(0, 0, 6 * 256);    /* FUND ON */
    run(0, &dv, buf, 48);
    zc = zc_rate(buf + 8 * 32, 40 * 32);
    set_knob(0, 0, 0);          /* FUND OFF */
    set_knob(0, 1, 18 * 256);   /* OCT ON: the sub alone */
    run(0, &dv, buf2, 48);
    {
        double z2 = zc_rate(buf2 + 8 * 32, 40 * 32);
        printf("info oct1 fund %f sub %f\n", zc, z2);
        CHECK("oct1-sub-octave", z2 < zc * 0.6);
    }
    defaults(0);

    /* 7. Loudness: a single saw at ON is about DNM_PEAK, so the switch
     * default (VCO1 ON + VCO2 -3 dB) lands near 1.7x DNM_PEAK. */
    dv.pitch = 69 << 16;
    run(0, &dv, buf, 8);
    {
        int32_t pk = peak32(buf + 32, 7 * 32);
        printf("info default-peak %d (DNM_PEAK %d)\n", pk, DNM_PEAK);
        CHECK("loudness", pk > DNM_PEAK && pk <= (int32_t)(DNM_PEAK * 2));
    }

    /* 8. Mono per track: the newest note owns the track, releasing it
     * returns to the still-held note, and a chord leaves one sounding. */
    {
        struct dnm_voice d0, d1, d2;
        static int32_t m0[32], m1[32];
        int b;

        ob1_ticks += 200;
        ob1_reap();
        fw_gate_off = 0;
        defaults(1); defaults(2); defaults(3);
        memset(&d0, 0, sizeof d0);
        memset(&d1, 0, sizeof d1);
        memset(&d2, 0, sizeof d2);
        d0.velocity = d1.velocity = d2.velocity = 100;
        d0.track = d1.track = d2.track = 0;
        d0.gate = d1.gate = d2.gate = 1;
        d0.pitch = 57 << 16;
        d1.pitch = 69 << 16;
        d2.pitch = 69 << 16;

        d0.fresh = 1;
        ob1_voice_on(1, 0, 0);
        for (b = 0; b < 4; b++) {
            fw_gate_off = 0;
            ob1_block(0);
            ob1_machine.render(1, m0, &d0);
            d0.fresh = 0;
        }

        d1.fresh = 1;
        ob1_voice_on(2, 0, 0);
        for (b = 0; b < 4; b++) {
            fw_gate_off = 0;
            ob1_block(0);
            ob1_machine.render(1, m0, &d0);
            ob1_machine.render(2, m1, &d1);
            d0.fresh = d1.fresh = 0;
        }
        CHECK("mono-old-silent", peak32(m0, 32) == 0);
        CHECK("mono-new-live", peak32(m1, 32) > 1000);

        d1.gate = 0;
        for (b = 0; b < 4; b++) {
            fw_gate_off = b == 0 ? (1u << 2) : 0;
            ob1_block(0);
            ob1_machine.render(1, m0, &d0);
            ob1_machine.render(2, m1, &d1);
        }
        CHECK("last-note-returns", peak32(m0, 32) > 1000);
        CHECK("released-new-silent", peak32(m1, 32) == 0);

        /* A chord: both fresh in one block; only the newest sounds. */
        ob1_ticks += 200;
        ob1_reap();
        fw_gate_off = 0;
        memset(&d0, 0, sizeof d0);
        memset(&d2, 0, sizeof d2);
        d0.velocity = d2.velocity = 100;
        d0.track = d2.track = 0;
        d0.gate = d2.gate = 1;
        d0.pitch = 57 << 16;
        d2.pitch = 69 << 16;
        d0.fresh = d2.fresh = 1;
        ob1_voice_on(1, 0, 0);
        ob1_voice_on(3, 0, 0);
        fw_gate_off = 0;
        ob1_block(0);
        ob1_machine.render(1, m0, &d0);
        ob1_machine.render(3, m1, &d2);
        d0.fresh = d2.fresh = 0;
        CHECK("mono-chord-one-live", peak32(m0, 32) == 0 && peak32(m1, 32) > 1000);
    }

    /* 8a. Last-note priority through a stack: C takes over from B, then
     * B from A; releasing C returns to B, releasing B returns to A. */
    {
        struct dnm_voice da, db, dc;
        static int32_t ma[32 * 17], mb[32 * 17], mc[32 * 17];
        int b;

        ob1_ticks += 200;
        ob1_reap();
        fw_gate_off = 0;
        defaults(1); defaults(2); defaults(3);
        memset(&da, 0, sizeof da);
        memset(&db, 0, sizeof db);
        memset(&dc, 0, sizeof dc);
        da.velocity = db.velocity = dc.velocity = 100;
        da.track = db.track = dc.track = 0;
        da.gate = db.gate = dc.gate = 1;
        da.pitch = 57 << 16;
        db.pitch = 60 << 16;
        dc.pitch = 64 << 16;

        da.fresh = 1;
        ob1_voice_on(1, 0, 0);
        for (b = 0; b < 3; b++) {
            fw_gate_off = 0;
            ob1_block(0);
            ob1_machine.render(1, ma + 32 * b, &da);
            da.fresh = 0;
        }
        db.fresh = 1;
        ob1_voice_on(2, 0, 0);
        for (b = 3; b < 6; b++) {
            fw_gate_off = 0;
            ob1_block(0);
            ob1_machine.render(1, ma + 32 * b, &da);
            ob1_machine.render(2, mb + 32 * b, &db);
            da.fresh = db.fresh = 0;
        }
        dc.fresh = 1;
        ob1_voice_on(3, 0, 0);
        for (b = 6; b < 9; b++) {
            fw_gate_off = 0;
            ob1_block(0);
            ob1_machine.render(1, ma + 32 * b, &da);
            ob1_machine.render(2, mb + 32 * b, &db);
            ob1_machine.render(3, mc + 32 * b, &dc);
            da.fresh = db.fresh = dc.fresh = 0;
        }
        dc.gate = 0;
        for (b = 9; b < 13; b++) {
            fw_gate_off = b == 9 ? (1u << 3) : 0;
            ob1_block(0);
            ob1_machine.render(1, ma + 32 * b, &da);
            ob1_machine.render(2, mb + 32 * b, &db);
            ob1_machine.render(3, mc + 32 * b, &dc);
        }
        CHECK("stack-c-to-b", peak32(mb + 32 * 12, 32) > 1000 &&
                              peak32(ma + 32 * 12, 32) == 0 &&
                              peak32(mc + 32 * 12, 32) == 0);
        db.gate = 0;
        for (b = 13; b < 17; b++) {
            fw_gate_off = b == 13 ? (1u << 2) : 0;
            ob1_block(0);
            ob1_machine.render(1, ma + 32 * b, &da);
            ob1_machine.render(2, mb + 32 * b, &db);
            ob1_machine.render(3, mc + 32 * b, &dc);
        }
        CHECK("stack-b-to-a", peak32(ma + 32 * 16, 32) > 1000 &&
                              peak32(mb + 32 * 16, 32) == 0 &&
                              peak32(mc + 32 * 16, 32) == 0);
    }

    /* 8b. Phase continuity: a note on another voice continues the track's
     * two VCOs. Compare against one oscillator state given the same pitch
     * history; after the crossfade the blocks are bit-identical. */
    {
        struct dnm_voice a, b;
        struct ob1_voice ref;
        const struct ob1_knobs k = {63, 63, 6, 3, 0, 0};
        static int32_t ma[32 * 8], mb[32 * 8], mr[32 * 8], tmp[32];
        int blk, i, diff = 0;

        ob1_ticks += 200;
        ob1_reap();
        fw_gate_off = 0;
        defaults(6); defaults(7);
        memset(&a, 0, sizeof a);
        memset(&b, 0, sizeof b);
        a.velocity = b.velocity = 100;
        a.track = b.track = 3;
        a.gate = b.gate = 1;
        a.pitch = 57 << 16;
        b.pitch = 69 << 16;
        a.fresh = 1;
        ob1_voice_on(6, 3, 0);
        for (blk = 0; blk < 6; blk++) {
            fw_gate_off = 0;
            ob1_block(0);
            ob1_machine.render(6, ma + 32 * blk, &a);
            a.fresh = 0;
        }
        b.fresh = 1;
        ob1_voice_on(7, 3, 0);
        for (blk = 0; blk < 6; blk++) {
            fw_gate_off = 0;
            ob1_block(0);
            ob1_machine.render(6, ma + 32 * blk, &a);
            ob1_machine.render(7, mb + 32 * blk, &b);
            a.fresh = b.fresh = 0;
        }
        ob1_voice_init(&ref, 4);
        for (blk = 0; blk < 6; blk++)
            ob1_voice_render(&ref, &k, dnm_phase_inc(57 << 16),
                             dnm_phase_inc(57 << 16), tmp);
        for (blk = 0; blk < 6; blk++)
            ob1_voice_render(&ref, &k, dnm_phase_inc(69 << 16),
                             dnm_phase_inc(69 << 16), mr + 32 * blk);
        for (i = 0; i < 32; i++) {
            int32_t d = mb[32 * 5 + i] - mr[32 * 5 + i];
            diff += d < 0 ? -d : d;
        }
        printf("info phase diff %d\n", diff);
        CHECK("phase-continuous", diff == 0);
    }

    /* 8c. Handover continuity: a triangle has no wraparound edge, so a
     * hard cut or a phase jump at the note change would show as a large
     * sample-to-sample step. The crossfade should stay near the steady
     * waveform's own slope. */
    {
        struct dnm_voice a, b;
        static int32_t ma[32 * 12], mb[32 * 12], mix[32 * 12];
        int blk, i, step, steady = 0, handover = 0;

        ob1_ticks += 200;
        ob1_reap();
        fw_gate_off = 0;
        defaults(2); defaults(3);
        set_knob(2, 4, 0);          /* VCO1 alone */
        set_knob(3, 4, 0);
        set_knob(2, 11, 0);         /* triangle */
        set_knob(3, 11, 0);
        memset(&a, 0, sizeof a);
        memset(&b, 0, sizeof b);
        a.velocity = b.velocity = 100;
        a.track = b.track = 2;
        a.gate = b.gate = 1;
        a.pitch = 57 << 16;
        b.pitch = 60 << 16;
        a.fresh = 1;
        ob1_voice_on(2, 2, 0);
        for (blk = 0; blk < 6; blk++) {
            fw_gate_off = 0;
            ob1_block(0);
            ob1_machine.render(2, ma + 32 * blk, &a);
            memset(mb + 32 * blk, 0, 32 * sizeof(int32_t));
            a.fresh = 0;
        }
        b.fresh = 1;
        ob1_voice_on(3, 2, 0);
        for (blk = 6; blk < 12; blk++) {
            fw_gate_off = 0;
            ob1_block(0);
            ob1_machine.render(2, ma + 32 * blk, &a);
            ob1_machine.render(3, mb + 32 * blk, &b);
            a.fresh = b.fresh = 0;
        }
        for (i = 0; i < 32 * 12; i++)
            mix[i] = ma[i] + mb[i];
        for (i = 32 * 2; i < 32 * 6; i++) {
            step = mix[i] - mix[i - 1];
            if (step < 0) step = -step;
            if (step > steady) steady = step;
        }
        for (i = 32 * 6; i < 32 * 12; i++) {
            step = mix[i] - mix[i - 1];
            if (step < 0) step = -step;
            if (step > handover) handover = step;
        }
        printf("info handover steps steady %d handover %d\n", steady, handover);
        CHECK("handover-continuous", handover <= steady * 2 + 1000000);
    }

    /* 9. Cross-track coexistence: one held note each, both live. */
    {
        struct dnm_voice d4;
        ob1_ticks += 200;
        ob1_reap();
        fw_gate_off = 0;
        memset(&d4, 0, sizeof d4);
        defaults(4);
        d4.velocity = 100; d4.track = 1; d4.gate = 1; d4.pitch = 69 << 16;
        d4.fresh = 1;
        ob1_voice_on(4, 1, 0);
        fw_gate_off = 0;
        ob1_block(0);
        ob1_machine.render(4, buf2, &d4);
        d4.fresh = 0;
        dv.gate = 1; dv.fresh = 1;
        ob1_voice_on(0, 0, 0);
        fw_gate_off = 0;
        ob1_block(0);
        ob1_machine.render(0, buf, &dv);
        dv.fresh = 0;
        fw_gate_off = 0;
        ob1_block(0);
        ob1_machine.render(0, buf, &dv);
        ob1_machine.render(4, buf2, &d4);
        CHECK("xtrack-both-live", peak32(buf, 32) > 0 && peak32(buf2, 32) > 0);
    }

    /* 10. Reap: a track the render stopped observing ages out on UI tick. */
    {
        ob1_ticks += 200;
        ob1_reap();
        fw_gate_off = 0;
        ob1_block(0);
        ob1_machine.render(0, buf, &dv);
        CHECK("reap-clears", peak32(buf, 32) == 0);
        dv.fresh = 1;
        ob1_voice_on(0, 0, 0);
        fw_gate_off = 0;
        ob1_block(0);
        ob1_machine.render(0, buf, &dv);
        dv.fresh = 0;
        CHECK("reap-restarts", peak32(buf, 32) > 0);
    }

    /* 11. Bounds: every knob at its stock maximum stays live and clipped. */
    {
        int i;
        for (i = 0; i < 16; i++)
            set_knob(5, i, 127 * 256);
        memset(&dv, 0, sizeof dv);
        dv.velocity = 127; dv.track = 0; dv.gate = 1; dv.pitch = 100 << 16;
        run(5, &dv, buf, 8);
        {
            int32_t pk = peak32(buf, 8 * 32);
            printf("info max-peak %d\n", pk);
            CHECK("extremes-max", pk > DNM_PEAK && pk <= (int32_t)(DNM_PEAK * 6));
        }
    }

    /* 12. Descriptor sanity: the Digitakt machine's fifteen controls. */
    CHECK("desc-id", ob1_machine.id == 69);
    CHECK("desc-name", ob1_machine.name[0] == 'O' && ob1_machine.name[2] == '-');
    CHECK("desc-knobs",
          ob1_machine.knobs[0].name && !strcmp(ob1_machine.knobs[0].name, "FND1") &&
          ob1_machine.knobs[8].name && !strcmp(ob1_machine.knobs[8].name, "FRQ1") &&
          ob1_machine.knobs[15].name && !strcmp(ob1_machine.knobs[15].name, "WAV2") &&
          ob1_machine.knobs[3].name == 0);
    CHECK("desc-flags", ob1_machine.flags == 0);
    CHECK("desc-defaults", ob1_machine.defaults[0] == 6 * 256 &&
          ob1_machine.defaults[4] == 63 * 256 &&
          ob1_machine.defaults[3] == -1 &&
          ob1_machine.defaults[8] == 64 * 256 &&
          ob1_machine.defaults[11] == 127 * 256 &&
          ob1_machine.defaults[15] == 127 * 256);

    /* 13. The MACHINES tile icon: a thin saw, not blank or a filled block. */
    {
        const uint16_t *ic = ob1_machine.icon;
        int r, lit = 0, rows = 0;
        for (r = 0; r < 16 && ic; r++) {
            lit += __builtin_popcount(ic[r]);
            rows += ic[r] != 0;
        }
        CHECK("desc-icon", ic && ic[0] == 0 && ic[15] == 0 &&
              lit > 12 && lit < 64 && rows >= 6);
    }

    /* 14. Knob icon art: every graphic draws something, and the wave
     * shapes differ. */
    {
        uint32_t a[17], b[17];
        int r, lit = 0;
        ob1_knob_wave(0, a); ob1_knob_wave(127, b);
        for (r = 0; r < 17; r++) lit |= (a[r] ^ b[r]);
        CHECK("art-wave", lit != 0);
        ob1_knob_frq(0, a); ob1_knob_frq(63, b);
        lit = 0;
        for (r = 0; r < 17; r++) lit |= (a[r] ^ b[r]);
        CHECK("art-frq", lit != 0);
        ob1_knob_mod(0, a); ob1_knob_mod(3, b);
        lit = 0;
        for (r = 0; r < 17; r++) lit |= (a[r] ^ b[r]);
        CHECK("art-mod", lit != 0);
        ob1_knob_mix(0, a); ob1_knob_mix(8, b);
        lit = 0;
        for (r = 0; r < 17; r++) lit |= (a[r] ^ b[r]);
        CHECK("art-mix", lit != 0);
        ob1_knob_noise(0, a); ob1_knob_noise(2, b);
        lit = 0;
        for (r = 0; r < 17; r++) lit |= (a[r] ^ b[r]);
        CHECK("art-noise", lit != 0);
    }

    printf(fails ? "\n%d FAILURES\n" : "\nALL ENGINE TESTS PASSED\n", fails);
    return fails != 0;
}
"""

CLI = COMMON + r"""
/* argv: note <16 knob values, 8.8, index order> lfodest lfomod nblocks.
 * Renders voice 0 from a fresh process (identical init every run) and
 * writes the raw int32 blocks to stdout. */
int main(int argc, char **argv) {
    struct dnm_voice dv;
    static int32_t out[32 * 64];
    long v[20];
    int b, nblocks, i;
    if (argc != 21) {
        fprintf(stderr, "usage: cli note <16 knobs> lfodest lfomod nblocks\n");
        return 2;
    }
    for (i = 0; i < 20; i++) v[i] = atol(argv[1 + i]);
    nblocks = (int)v[19];
    if (nblocks < 1 || nblocks > 64) return 2;
    memset(&dv, 0, sizeof dv);
    dv.velocity = 100; dv.track = 0; dv.gate = 1;
    dv.pitch = (int32_t)(v[0] << 16);
    for (i = 0; i < 16; i++)
        set_knob(0, i, (int32_t)v[1 + i]);
    fw_lfo_state[0 + 16] = (int32_t)v[17];
    fw_lfo_state[0 + 17] = (int32_t)v[18];
    fw_lfo_state[10 + 16] = 127;
    fw_lfo_state[10 + 17] = 0;
    dv.fresh = 1;
    ob1_voice_on(0, 0, 0);
    for (b = 0; b < nblocks; b++) {
        fw_gate_off = 0;
        ob1_block(0);
        ob1_machine.render(0, out + 32 * b, &dv);
        dv.fresh = 0;
    }
    fwrite(out, 4, (size_t)(32 * nblocks), stdout);
    return 0;
}
"""


def build(tmp, name, src):
    drv = tmp / (name + ".c")
    exe = tmp / name
    drv.write_text(src)
    r = subprocess.run(
        ["cc", "-O1", "-o", str(exe), str(drv),
         str(ROOT / "machines" / "digitone" / "engine.c"),
         str(ROOT / "dsp" / "dsp.c"),
         str(ROOT / "dsp" / "voice.c"),
         str(ROOT / "machines" / "digitakt" / "knobs.c"),
         "-I", str(INC),
         "-I", str(ROOT / "machines" / "digitone"),
         "-I", str(ROOT / "dsp"),
         "-I", str(ROOT / "machines" / "digitakt"),
         "-lm"],
        capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout[-3000:])
        print(r.stderr[-3000:])
        raise SystemExit("engine host compile failed: " + name)
    return exe


def main():
    fails = 0

    def check(name, cond):
        nonlocal fails
        print(("PASS " if cond else "FAIL ") + name)
        if not cond:
            fails += 1

    with tempfile.TemporaryDirectory() as td:
        tmp = pathlib.Path(td)
        exe = build(tmp, "ob1test", DRIVER)
        r = subprocess.run([str(exe)], capture_output=True, text=True)
        print(r.stdout, end="")
        print(r.stderr, end="")
        if r.returncode != 0:
            fails += 1
        cli = build(tmp, "ob1cli", CLI)

        W = lambda r: r * 256

        def knobs(fnd1=6, oct1=0, xmod=0, fnd2=63, oct2=0, sync=0, nois=0,
                  frq1=64, fin1=64, typ1=0, wav1=127,
                  frq2=64, fin2=64, typ2=0, wav2=127):
            # SYN1 A-H (MIX) then SYN2 A-H (OSC); slot 20 is blank.
            return [W(fnd1), W(oct1), W(xmod), 0, W(fnd2), W(oct2), W(sync),
                    W(nois), W(frq1), W(fin1), W(typ1), W(wav1), W(frq2),
                    W(fin2), W(typ2), W(wav2)]

        def render(note=69, lfo=(127, 0), nblocks=8, **kw):
            args = [note] + knobs(**kw) + [lfo[0], lfo[1], nblocks]
            r = subprocess.run([str(cli)] + [str(a) for a in args],
                               capture_output=True)
            assert r.returncode == 0, r.stderr[-500:]
            assert len(r.stdout) == 32 * nblocks * 4
            return r.stdout

        def located_diff(a, b):
            xa = struct.unpack("<%di" % (len(a) // 4), a)
            xb = struct.unpack("<%di" % (len(b) // 4), b)
            return sum(abs((x >> 14) - (y >> 14)) for x, y in zip(xa, xb))

        def peak(a):
            return max(abs(x) for x in struct.unpack("<%di" % (len(a) // 4), a))

        # Deterministic replay: same args, byte-identical bytes.
        check("replay-identical", render() == render())

        # WAV: triangle/saw/square on VCO1 alone differ, within each family.
        v1 = dict(fnd2=0)
        tri = render(wav1=0, typ1=0, **v1)
        saw = render(wav1=127, typ1=0, **v1)
        sqr = render(wav1=127, typ1=64, **v1)
        check("wave-tri-saw", located_diff(tri, saw) > 20000)
        check("wave-saw-sqr", located_diff(saw, sqr) > 20000)

        # SYNC: VCO2 alone (VCO1 fund off, VCO2 on) at +7 semis changes it.
        vco2 = dict(fnd1=0, fnd2=89, frq2=78)
        check("sync-matters",
              located_diff(render(sync=0, **vco2), render(sync=127, **vco2)) > 20000)
        # XMOD: both VCOs live, cross mod moves VCO1.
        both = dict(fnd1=6, fnd2=89)
        check("xmod-matters",
              located_diff(render(xmod=0, **both), render(xmod=35, **both)) > 20000)

        # Mixer: everything off is silent (bit-exact from init);
        # noise alone is live.
        check("mix-off-silent",
              peak(render(fnd1=0, oct1=0, fnd2=0, oct2=0, nois=0)) == 0)
        check("noise-live", peak(render(fnd1=0, oct1=0, fnd2=0, oct2=0, nois=127)) > 1000)

        # LFO: the WAV1 slot (30) changes the timbre (from mid-scale, so the
        # modulation has room); an AMP slot (66) is ignored downstream
        # (bit-identical).
        check("lfo-wav",
              located_diff(render(wav1=64), render(wav1=64, lfo=(30, 8192))) > 20000)
        check("lfo-amp-ignored", render() == render(lfo=(66, 8192)))

        # Extremes at min go silent (mixes and noise off).
        check("extremes-min-silent",
              peak(render(fnd1=0, oct1=0, fnd2=0, oct2=0, nois=0, wav1=0,
                          wav2=0, frq1=0, frq2=0, fin1=0, fin2=0)) == 0)

    print("\n%d FAILURES" % fails if fails else "\nALL HOST TESTS PASSED")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
