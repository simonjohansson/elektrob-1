/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Print the knob graphics as text, side by side at representative settings: `mise run knobs`. */
#include <stdio.h>
#include "knobs.h"

static void show(const char *title, void (*fn)(int32_t, uint32_t *), const int32_t *vals, const char *const *names, int n)
{
    uint32_t rows[8][KNOB_PX];
    int i, r, c;
    printf("%s\n", title);
    for (i = 0; i < n; i++) {
        fn(vals[i], rows[i]);
        printf("%-20s", names[i]);
    }
    printf("\n");
    for (r = 0; r < KNOB_PX; r++) {
        for (i = 0; i < n; i++) {
            for (c = 0; c < KNOB_PX; c++)
                fputs(rows[i][r] >> (KNOB_PX - 1 - c) & 1 ? "█" : "·", stdout);
            printf("   ");
        }
        printf("\n");
    }
    printf("\n");
}

int main(void)
{
    static const int32_t wv[] = {0, 32, 63, 64, 96, 127};
    static const char *const wn[] = {"S0 triangle", "S32", "S63 saw", "P0 6.7 %", "P32", "P63 square"};
    static const int32_t fv[] = {0, 20, 32, 39, 44, 63};
    static const char *const fn[] = {"-32", "-12", "0", "+7", "+12", "+31"};
    static const int32_t mv[] = {0, 1, 2, 3};
    static const char *const mn[] = {"OFF", "SYNC", "XMOD", "BOTH"};
    static const int32_t xv[] = {0, 6, 4, 7, 2, 8};
    static const char *const xn[] = {"-/-", "H/-", "L/L", "H/L", "-/H", "H/H"};
    static const int32_t nv[] = {0, 1, 2};
    static const char *const nn[] = {"OFF", "LO", "HI"};
    show("WAV1 / WAV2", ob1_knob_wave, wv, wn, 6);
    show("FRQ2", ob1_knob_frq, fv, fn, 6);
    show("MOD", ob1_knob_mod, mv, mn, 4);
    show("MIX1 / MIX2", ob1_knob_mix, xv, xn, 6);
    show("NOIS", ob1_knob_noise, nv, nn, 3);
    return 0;
}
