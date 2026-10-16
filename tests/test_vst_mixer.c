/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "voice.h"

/* Explicit reference -3 dB settings, rather than normalized knob position 0.5. */
int main(void)
{
    const int waves[] = {0, 32, 63, 64, 127};
    const double hz[] = {55, 220, 1760};
    int oscillator, sub, wave, pitch, level, block, i, cases = 0;
    double worst = 0;
    for (oscillator = 0; oscillator < 2; oscillator++)
        for (sub = 0; sub < 2; sub++)
            for (wave = 0; wave < 5; wave++)
                for (pitch = 0; pitch < 3; pitch++) {
                    double power[2] = {0};
                    uint32_t inc = (uint32_t)(hz[pitch] * 4294967296.0 / 48000);
                    for (level = 1; level <= 2; level++) {
                        struct ob1_voice v;
                        struct ob1_knobs k = {waves[wave], waves[wave], 0, 0, 0, 0};
                        int32_t out[32];
                        if (oscillator) k.mix2 = level * (sub ? 1 : 3);
                        else k.mix1 = level * (sub ? 1 : 3);
                        ob1_voice_init(&v, 1);
                        for (block = 0; block < 1800; block++) {
                            ob1_voice_render(&v, &k, inc, inc, out);
                            if (block < 300) continue;
                            for (i = 0; i < 32; i++) power[level - 1] += (double)out[i] * out[i];
                        }
                    }
                    double error = fabs(10 * log10(power[0] / power[1]) + 3);
                    assert(error < .025);
                    if (error > worst) worst = error;
                    cases++;
                }
    {
        double power[2] = {0};
        for (level = 1; level <= 2; level++) {
            struct ob1_voice v;
            struct ob1_knobs k = {0, 0, 0, 0, 0, level};
            int32_t out[32];
            ob1_voice_init(&v, 1);
            for (block = 0; block < 1800; block++) {
                ob1_voice_render(&v, &k, 0, 0, out);
                if (block < 300) continue;
                for (i = 0; i < 32; i++) power[level - 1] += (double)out[i] * out[i];
            }
        }
        assert(fabs(10 * log10(power[0] / power[1]) + 3) < .025);
    }
    printf("Mixer: %d oscillator/sub cases and noise match -3 dB, worst error %.4f dB\n", cases, worst);
    return 0;
}
