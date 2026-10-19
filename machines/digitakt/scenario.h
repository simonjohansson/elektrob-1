/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef OB1_DIGITAKT_SCENARIO_H
#define OB1_DIGITAKT_SCENARIO_H
#include <stdint.h>

/* Diagnostic DSP clock; stock AMP stays held and saved parameters stay untouched. */
struct ob1_scenario {
    uint32_t block;
    uint8_t note, gate, controls[15];
};

static int ob1_scenario_step(struct ob1_scenario *s)
{
    if (!s->block)
        return 0;
    uint32_t block = s->block++ - 1;
    if (block == 23 * 1500) {
        s->block = 0;
        return 0;
    }
    switch (block) {
    case 0: {
        static const uint8_t initial[15] = {32, 64, 0, 63, 32, 64, 0, 63, 106, 21, 0, 64, 21, 0, 64};
        for (int i = 0; i < 15; i++) s->controls[i] = initial[i];
        s->note = 57; s->gate = 1;
        break;
    }
    case 3 * 1500: s->controls[9] = 106; break;
    case 4 * 1500: s->controls[12] = 106; break;
    case 5 * 1500: s->controls[11] = 21; break;
    case 6 * 1500: s->controls[11] = 64; s->controls[9] = s->controls[12] = 21; break;
    case 7 * 1500: s->note = 64; break;
    case 8 * 1500: s->controls[3] = 0; break;
    case 9 * 1500: s->controls[2] = 1; s->controls[3] = 32; break;
    case 10 * 1500: s->controls[6] = 1; s->controls[7] = 16; break;
    case 11 * 1500: s->note = 57; break;
    case 12 * 1500: s->controls[4] = 39; s->controls[13] = 127; break;
    case 13 * 1500: s->controls[13] = 0; s->controls[10] = 127; break;
    case 14 * 1500: s->controls[10] = 0; s->controls[14] = 106; break;
    case 15 * 1500: s->controls[14] = 21; break;
    case 16 * 1500: s->controls[2] = s->controls[6] = 0;
        s->controls[3] = s->controls[7] = 63; s->controls[4] = 32; break;
    case 17 * 1500: s->note = 45; break;
    case 18 * 1500: s->gate = 0; break;
    case 20 * 1500: s->note = 81; s->gate = 1; break;
    case 21 * 1500: s->gate = 0; break;
    }
    return 1;
}
#endif
