/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Render the machine's DSP to a 48 kHz 16-bit mono WAV: render FREQ WAVE SECONDS OUT.wav
 * WAVE as WAV1: 0-63 saw type, triangle to saw; 64-127 pulse type, 10 % pulse to square. */
#include <stdio.h>
#include <stdlib.h>
#include "dsp.h"

#define RATE 48000

static void put32(FILE *f, uint32_t v) { fputc(v, f); fputc(v >> 8, f); fputc(v >> 16, f); fputc(v >> 24, f); }
static void put16(FILE *f, uint32_t v) { fputc(v, f); fputc(v >> 8, f); }

int main(int argc, char **argv)
{
    double freq, seconds;
    int32_t wave, block[32], sub[32];
    struct dsp_osc o = {{0}, 0, 32767, 32767};
    uint32_t inc, frames, i, n;
    FILE *f;
    if (argc != 5) {
        fprintf(stderr, "usage: render FREQ WAVE SECONDS OUT.wav\n");
        return 2;
    }
    freq = atof(argv[1]);
    wave = atoi(argv[2]);
    seconds = atof(argv[3]);
    if (freq <= 0 || freq >= RATE / 2 || wave < 0 || wave > 127 || seconds <= 0) {
        fprintf(stderr, "render: FREQ 0-24000, WAVE 0-127, SECONDS > 0\n");
        return 2;
    }
    if (!(f = fopen(argv[4], "wb"))) {
        perror(argv[4]);
        return 1;
    }
    inc = (uint32_t)(freq / RATE * 4294967296.0);
    frames = (uint32_t)(seconds * RATE) / 32 * 32;
    fputs("RIFF", f); put32(f, 36 + 2 * frames); fputs("WAVEfmt ", f);
    put32(f, 16); put16(f, 1); put16(f, 1); put32(f, RATE); put32(f, 2 * RATE); put16(f, 2); put16(f, 16);
    fputs("data", f); put32(f, 2 * frames);
    for (n = 0; n < frames; n += 32) {
        if (wave < 64)
            dsp_saw_type(&o, inc, -32768 + (wave << 16) / 63, block, sub, 32);
        else
            dsp_pulse_type(&o, inc, 429496730u + (uint32_t)(wave - 64) * ((0x80000000u - 429496730u) / 63), block, sub, 32);
        for (i = 0; i < 32; i++)
            put16(f, (uint32_t)(block[i] > 32767 ? 32767 : block[i] < -32768 ? -32768 : block[i]));
    }
    if (fclose(f)) {
        perror(argv[4]);
        return 1;
    }
    printf("%s\n", argv[4]);
    return 0;
}
