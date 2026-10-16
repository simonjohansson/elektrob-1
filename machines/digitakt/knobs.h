/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The SRC page's knob graphics (#18, the A designs), with no Digitakt dependencies: each is 17 rows, the top
 * one first, of 17 columns, bit 16 the leftmost. ob1.c draws them; tools/knobs.c prints them. */
#ifndef KNOBS_H
#define KNOBS_H
#include <stdint.h>

#define KNOB_PX 17

void knob_wave(int32_t w, uint32_t *rows);      /* WAV1/WAV2 0-127: two cycles of the shape */
void knob_frq(int32_t f, uint32_t *rows);       /* FRQ2 0-63: a centre-zero meter, ticks at the octaves */
void knob_mod(int32_t m, uint32_t *rows);       /* MOD 0-3: VCO1 and VCO2, sync's and cross mod's arrows */
void knob_mix(int32_t m, uint32_t *rows);       /* MIX1/MIX2 0-8: two faders, FUND and OCT */
void knob_noise(int32_t n, uint32_t *rows);     /* NOIS 0-2: noise, its height by level */

#endif
