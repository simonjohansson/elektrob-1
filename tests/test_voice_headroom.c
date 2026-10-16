/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include "voice.h"

static uint32_t step(double hz)
{
    return (uint32_t)(hz * 4294967296.0 / 48000.0);
}

int main(void)
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
    return 0;
}
