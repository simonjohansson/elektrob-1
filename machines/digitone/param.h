/* SPDX-License-Identifier: GPL-2.0-or-later */
/* OB-1 Digitone knob mapping: the same fifteen controls as the Digitakt
 * machine (machines/digitakt/ob1.c), on the stock SYN parameters of an OB-1
 * sound. The Digitakt mints its own parameters; a Digitone machine cannot,
 * so each control borrows a stock SYN slot and works in that slot's stock
 * range (derived from the 1.44 parameter records in EVIDENCE.md).
 *
 * Two pages, as the Digitakt's own OSC and MIX pages:
 *   SYN1 A-H (MIX): FND1 OCT1 XMOD  --  FND2 OCT2 SYNC NOIS
 *   SYN2 A-H (OSC): FRQ1 FIN1 TYP1 WAV1 FRQ2 FIN2 TYP2 WAV2
 * SYN2's slots are exact 0-127; SYN1's are FM's (ALGO 0-7, the ratios,
 * HARM 37-89, DTUN/FDBK/MIX 0-127), and every one has enough states for
 * its control. A Digitone machine cannot host the OSC page on SYN1: ALGO
 * (0-7) is far too coarse for FRQ1's +-32 semitones.
 *
 * Included by engine.c (the render) and overlay.c (the pages), so the
 * displayed setting is always the sounded one. Pure 32-bit integer math:
 * safe at interrupt level and without a C library. */
#ifndef OB1_PARAM_H
#define OB1_PARAM_H
#include <stdint.h>

#define OB1_KNOBS 16                /* = machines.h's DNM_KNOBS */

/* Knob indices: SYN1 A-H first (the MIX page), then SYN2 A-H (the OSC
 * page), in the order the companion and the pages use. */
#define K_FND1  0
#define K_OCT1  1
#define K_XMOD  2
#define K_BLANK 3                   /* SYN1 D: no OB-1 control */
#define K_FND2  4
#define K_OCT2  5
#define K_SYNC  6
#define K_NOIS  7
#define K_FRQ1  8
#define K_FIN1  9
#define K_TYP1  10
#define K_WAV1  11
#define K_FRQ2  12
#define K_FIN2  13
#define K_TYP2  14
#define K_WAV2  15

/* Knob index -> sound slot, and the slot's stock raw range (min, max). */
static const uint8_t ob1_slot[OB1_KNOBS] = {
    17, 18, 19, 20, 21, 22, 23, 24, 27, 28, 29, 30, 31, 32, 33, 34,
};
static const uint8_t ob1_rmin[OB1_KNOBS] = {
    0, 0, 0, 0, 37, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
static const uint8_t ob1_rmax[OB1_KNOBS] = {
    7, 18, 35, 127, 89, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127, 127,
};

/* What the knobs are set to when a track switches to OB-1, in each knob's
 * stock 8.8 domain (-1 leaves the knob alone): both VCOs saws in unison,
 * VCO1's FUND ON, VCO2's at -3 dB, no sync or cross mod, noise off. The
 * overlay also writes these on a FUNC+turn, where the stock reset would
 * otherwise write the borrowed stock record's default (FM's, not OB-1's:
 * DECA/DECB are 32, ATKA/ATKB 0, ENDA/ENDB 127, LEVA/LEVB 0). */
static const int16_t ob1_defaults[OB1_KNOBS] = {
    6 << 8, 0, 0, -1, 63 << 8, 0, 0, 0,
    64 << 8, 64 << 8, 0, 127 << 8, 64 << 8, 64 << 8, 0, 127 << 8,
};

/* The knob's raw value as 0-`max` across its slot's stock range (an equal
 * share each), and as one of `n` equal zones: the Digitakt machine's
 * knob()/zone(), widened to the slot's own range. */
static inline int32_t ob1_span(int32_t i, int32_t r, int32_t max)
{
    int32_t lo = ob1_rmin[i];
    r = r < lo ? lo : r > ob1_rmax[i] ? ob1_rmax[i] : r;
    return (r - lo) * max / (ob1_rmax[i] - lo + 1);
}
static inline int32_t ob1_zone(int32_t i, int32_t r, int32_t n)
{
    return ob1_span(i, r, n);
}

/* FRQ1/FRQ2 (raw 0-127): the coarse pitch, -32..+31 semitones, two steps a
 * semitone, exact zero at 64. */
static inline int32_t ob1_frq_of(int32_t r)
{
    r = r < 0 ? 0 : r > 127 ? 127 : r;
    return (r >> 1) - 32;
}

/* FIN1/FIN2 (raw 0-127, 64 centred): fine tune, Q16 semitones, +-50 cents. */
static inline int32_t ob1_fine_of(int32_t r)
{
    int32_t v;
    r = r < 0 ? 0 : r > 127 ? 127 : r;
    v = (r << 8) - 0x4000;
    return v <= 0 ? v * 2 : v * 128 / 63;
}

/* TYP1/TYP2 (raw 0-127): the family, saw below the midpoint, pulse above. */
static inline int32_t ob1_family_of(int32_t r)
{
    return r >= 64;
}

/* WAV1/WAV2 (raw 0-127) with its TYP: 0-63 saw type, 64-127 pulse type. */
static inline int32_t ob1_wave_of(int32_t typ, int32_t pos)
{
    pos = pos < 0 ? 0 : pos > 127 ? 127 : pos;
    return ob1_family_of(typ) * 64 + (pos >> 1);
}

#endif
