/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Render the machine's voice (machine/voice.c) to a 48 kHz mono WAV, 24-bit:
 *     render OUT.wav [key=value ...]
 * note (MIDI, 69 = A4 440 Hz; default 57, A3), seconds (4), and the SRC page's knobs as the machine reads
 * them: wav1, wav2 (0-127), frq2 (0-63, 32 unison), mix1, mix2 (0-8), mod (0-3), nois (0-2). Defaults are a
 * switch's: wav1 63, wav2 63, frq2 32, mix1 6, mix2 3, mod 0, nois 0. sweep=KNOB:FROM:TO moves a knob
 * linearly from FROM to TO over the render, a block at a time, as an automated knob would. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "voice.h"

#define RATE 48000

static void put32(FILE *f, uint32_t v) { fputc(v, f); fputc(v >> 8, f); fputc(v >> 16, f); fputc(v >> 24, f); }
static void put16(FILE *f, uint32_t v) { fputc(v, f); fputc(v >> 8, f); }

static int32_t *field(struct ob1_knobs *k, const char *name, int32_t *frq2)
{
    if (!strcmp(name, "wav1")) return &k->wav1;
    if (!strcmp(name, "wav2")) return &k->wav2;
    if (!strcmp(name, "mix1")) return &k->mix1;
    if (!strcmp(name, "mix2")) return &k->mix2;
    if (!strcmp(name, "mod")) return &k->mod;
    if (!strcmp(name, "nois")) return &k->nois;
    if (!strcmp(name, "frq2")) return frq2;
    return 0;
}

static int32_t clampv(const char *name, int32_t v)
{
    int32_t max = !strcmp(name, "frq2") ? 63 : !strncmp(name, "mix", 3) ? 8 : !strcmp(name, "mod") ? 3 :
                  !strcmp(name, "nois") ? 2 : 127;
    return v < 0 ? 0 : v > max ? max : v;
}

int main(int argc, char **argv)
{
    struct ob1_knobs k = {63, 63, 6, 3, 0, 0};
    struct ob1_voice v;
    int32_t frq2 = 32, block[32], *sw = 0, from = 0, to = 0;
    double note = 57, seconds = 4;
    uint32_t frames, n, i;
    FILE *f;
    int a;
    if (argc < 2) {
        fprintf(stderr, "usage: render OUT.wav [note=57] [seconds=4] [wav1=63] [wav2=63] [frq2=32] [mix1=6] "
                        "[mix2=3] [mod=0] [nois=0] [sweep=KNOB:FROM:TO]\n");
        return 2;
    }
    for (a = 2; a < argc; a++) {
        char name[8];
        int value, x, y;
        int32_t *p;
        if (sscanf(argv[a], "sweep=%7[a-z0-9]:%d:%d", name, &x, &y) == 3 && (p = field(&k, name, &frq2))) {
            sw = p, from = clampv(name, x), to = clampv(name, y);
        } else if (sscanf(argv[a], "note=%lf", &note) == 1 || sscanf(argv[a], "seconds=%lf", &seconds) == 1)
            ;
        else if (sscanf(argv[a], "%7[a-z0-9]=%d", name, &value) == 2 && (p = field(&k, name, &frq2)))
            *p = clampv(name, value);
        else {
            fprintf(stderr, "render: not understood: %s\n", argv[a]);
            return 2;
        }
    }
    if (seconds <= 0 || note < 0 || note > 127) {
        fprintf(stderr, "render: seconds > 0, note 0-127\n");
        return 2;
    }
    if (!(f = fopen(argv[1], "wb"))) {
        perror(argv[1]);
        return 1;
    }
    ob1_voice_init(&v, 1);
    frames = (uint32_t)(seconds * RATE) / 32 * 32;
    fputs("RIFF", f); put32(f, 36 + 3 * frames); fputs("WAVEfmt ", f);
    put32(f, 16); put16(f, 1); put16(f, 1); put32(f, RATE); put32(f, 3 * RATE); put16(f, 3); put16(f, 24);
    fputs("data", f); put32(f, 3 * frames);
    for (n = 0; n < frames; n += 32) {
        double hz1, hz2;
        if (sw)
            *sw = from + (int32_t)lround((double)(to - from) * n / frames);
        hz1 = fmin(440 * pow(2, (note - 69) / 12), 12000);
        hz2 = fmin(hz1 * pow(2, (frq2 - 32) / 12.0), 12000);
        ob1_voice_render(&v, &k, (uint32_t)(hz1 / RATE * 4294967296.0), (uint32_t)(hz2 / RATE * 4294967296.0),
                         block);
        for (i = 0; i < 32; i++) {
            uint32_t s = (uint32_t)(block[i] >> 8);
            fputc(s, f); fputc(s >> 8, f); fputc(s >> 16, f);
        }
    }
    if (fclose(f)) {
        perror(argv[1]);
        return 1;
    }
    printf("%s\n", argv[1]);
    return 0;
}
