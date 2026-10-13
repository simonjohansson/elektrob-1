/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Render the machine's DSP to a 48 kHz 16-bit mono WAV: render FREQ SHAPE SECONDS OUT.wav */
#include <stdio.h>
#include <stdlib.h>
#include "dsp.h"

#define RATE 48000

static void put32(FILE *f, uint32_t v) { fputc(v, f); fputc(v >> 8, f); fputc(v >> 16, f); fputc(v >> 24, f); }
static void put16(FILE *f, uint32_t v) { fputc(v, f); fputc(v >> 8, f); }

int main(int argc, char **argv)
{
    double freq, seconds;
    int32_t shape, block[32];
    uint32_t phase = 0, inc, frames, i, n;
    FILE *f;
    if (argc != 5) {
        fprintf(stderr, "usage: render FREQ SHAPE SECONDS OUT.wav\n");
        return 2;
    }
    freq = atof(argv[1]);
    shape = atoi(argv[2]);
    seconds = atof(argv[3]);
    if (freq <= 0 || freq >= RATE / 2 || shape < 0 || shape > 127 || seconds <= 0) {
        fprintf(stderr, "render: FREQ 0-24000, SHAPE 0-127, SECONDS > 0\n");
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
        dsp_render(&phase, inc, shape, 1 << 14, block, 32);
        for (i = 0; i < 32; i++)
            put16(f, (uint32_t)(block[i] >> 16));
    }
    if (fclose(f)) {
        perror(argv[4]);
        return 1;
    }
    printf("%s\n", argv[4]);
    return 0;
}
