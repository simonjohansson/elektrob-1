/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include "../machines/digitakt/pitch.h"

static uint32_t table[14849];

int main(void)
{
    uint32_t inc, last = 0;
    double worst = 0;
    int i, note;
    for (i = 0; i <= 14848; i++) {
        double value = pow(2, (i - 10752) / 2048.0) * (1u << 29);
        table[i] = value >= INT32_MAX ? INT32_MAX : (uint32_t)value;
    }
    assert(table[512] == 0x01000000 && table[10752] == 0x20000000 && table[14848] == 0x7fffffff);
    for (i = 0; i <= (117 << 16); i += 37) {
        double hz = 440 * pow(2, (i / 65536.0 - 72) / 12);
        double error;
        inc = ob1_pitch_inc(i, table);
        assert(inc >= last && inc <= DSP_INC_MAX);
        error = fabs(1200 * log2(inc * 48000.0 / 4294967296.0 / fmin(hz, 12000)));
        assert(error < .6);
        if (error > worst) worst = error;
        last = inc;
    }
    last = 0;
    for (i = (87 << 16) - 384; i <= (87 << 16) + 384; i++) {
        inc = ob1_pitch_inc(i, table);
        assert(inc >= last);
        last = inc;
    }
    last = 0;
    assert(ob1_pitch_inc(INT32_MIN, table) == ob1_pitch_inc(0, table));
    for (note = 0; note <= 160; note++) {
        double hz = 440 * pow(2, (note - 69) / 12.0);
        inc = ob1_pitch_inc((note + 3) << 16, table);
        assert(inc >= last && inc <= DSP_INC_MAX);
        assert(fabs(1200 * log2(inc * 48000.0 / 4294967296.0 / fmin(hz, 12000))) < .4);
        if (note == 48 || note == 84 || note == 85 || note == 96 || note == 120 || note == 127)
            printf("Digitakt MIDI %d: %.4f Hz\n", note, inc * 48000.0 / 4294967296.0);
        last = inc;
    }
    assert(ob1_pitch_inc((96 + 3) << 16, table) == 2 * ob1_pitch_inc((84 + 3) << 16, table));
    assert(ob1_pitch_inc((127 + 3) << 16, table) == DSP_INC_MAX);
    assert(ob1_pitch_inc(INT32_MAX, table) == DSP_INC_MAX);
    printf("Digitakt pitch: standard tuning, fractional accuracy and ceiling passed; worst %.4f cents\n", worst);
    return 0;
}
