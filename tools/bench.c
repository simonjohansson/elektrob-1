/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The machine's voice (machine/voice.c), for the ColdFire: tools/bench.py runs bench() in Unicorn and counts
 * its instructions a block (`mise run bench`). ob1.c's knob reads and pitch lookups are left out: a few
 * dozen instructions a voice. */
#include "voice.h"

static struct ob1_voice voice[8];
static int32_t out[8 * 32];

/* nblocks blocks of `voices` voices in one setting:
 * 0  the worst case: VCO1 pulse type and VCO2 saw type, both with their subs ON, BOTH, noise ON
 * 1  the default sound: VCO1, a saw, FUND ON
 * 2  both VCOs and their subs ON, no sync or cross mod, no noise */
void bench(int nblocks, int voices, int setting)
{
    static const struct ob1_knobs knobs[3] = {
        {100, 32, 8, 8, 3, 2},
        {63, 63, 6, 0, 0, 0},
        {63, 63, 8, 8, 0, 0},
    };
    int b, t;
    for (t = 0; t < voices; t++)
        ob1_voice_init(&voice[t], t + 1);
    for (b = 0; b < nblocks; b++)
        for (t = 0; t < voices; t++)                                         /* about C4 and G4 */
            ob1_voice_render(&voice[t], &knobs[setting], 23410327u + 977u * t, 35115490u - 1013u * t,
                             &out[32 * t]);
}
