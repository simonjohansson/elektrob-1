/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef OB1_DIGITAKT_PITCH_H
#define OB1_DIGITAKT_PITCH_H
#include "../../dsp/dsp.h"
static inline uint32_t ob1_table_inc(int32_t pitch, const uint32_t *table)
{
    uint32_t value = table[(uint32_t)pitch / 384];
    /* Middle C is 23409859 phase steps at 48 kHz. Split the product to retain the table's
     * fractional precision without 64-bit multiply helpers. */
    return (value >> 16) * (23409859u >> 13)
         + (((value & 65535u) * (23409859u >> 13)) >> 16)
         + (((value >> 13) * (23409859u & 8191u)) >> 16);
}
/* Keep octave extension out of the common pitch lookup's register allocation. */
static __attribute__((noinline)) uint32_t ob1_high_pitch_inc(int32_t pitch, const uint32_t *table)
{
    uint32_t octaves = 0, inc;
    /* Four octaves beyond the table's top already exceeds the oscillator ceiling. */
    if (pitch >= (135 << 16))
        return DSP_INC_MAX;
    do {
        pitch -= 12 << 16;
        octaves++;
    } while (pitch > (87 << 16));
    inc = ob1_table_inc(pitch, table);
    return inc > (DSP_INC_MAX >> octaves) ? DSP_INC_MAX : inc << octaves;
}
/* Table-relative semitones, Q16: unity at 63, corresponding to MIDI C4 plus the wrapper's offset. */
static inline uint32_t ob1_pitch_inc(int32_t pitch, const uint32_t *table)
{
    if (pitch > (87 << 16))
        return ob1_high_pitch_inc(pitch, table);
    pitch = pitch < 0 ? 0 : pitch;
    return ob1_table_inc(pitch, table);
}
#endif
