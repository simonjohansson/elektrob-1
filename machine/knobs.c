/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "knobs.h"
#include "voice.h"

#define BIT(c)  (1u << (KNOB_PX - 1 - (c)))         /* column c, 0 the leftmost */

static void clear(uint32_t *rows)
{
    int r;
    for (r = 0; r < KNOB_PX; r++)
        rows[r] = 0;
}

/* Rows r0 to r1 (either order) of column c. */
static void vline(uint32_t *rows, int c, int r0, int r1)
{
    int r;
    if (r0 > r1)
        r = r0, r0 = r1, r1 = r;
    for (r = r0; r <= r1; r++)
        rows[r] |= BIT(c);
}

/* The shape's value at phase p (Q16, 0-65536: the cycle's end is its own, not the next's start), without its
 * band-limiting: the saw type's max(s, 0) + g min(s, 0), the pulse type's +-1. Q15. */
static int32_t shape_at(const struct dsp_shape *sh, int32_t p)
{
    int32_t s;
    if (sh->pulse)
        return (uint32_t)p << 16 < sh->width && p < 65536 ? 32767 : -32768;
    s = p - 32768;                              /* the ramp, -1 to +1 */
    return s > 0 ? s : (sh->g * s) >> 15;
}

void knob_wave(int32_t w, uint32_t *rows)
{
    struct dsp_shape sh;
    int32_t v[KNOB_PX], lo = 32767, hi = -32768, c, r, prev = 0;
    ob1_shape(&sh, w);
    for (c = 0; c < KNOB_PX; c++) {             /* 8 columns a cycle: the first 0-8, ending on its own last
                                                   value, the second from 9 */
        int32_t p = c <= 8 ? c << 13 : (c - 9) << 13;
        v[c] = shape_at(&sh, p);
        lo = v[c] < lo ? v[c] : lo;
        hi = v[c] > hi ? v[c] : hi;
    }
    clear(rows);
    for (c = 0; c < KNOB_PX; c++) {             /* the shape's own range over rows 14 (low) to 1 (high) */
        r = hi > lo ? 14 - (int32_t)(((v[c] - lo) * 13 + (hi - lo) / 2) / (hi - lo)) : 8;
        vline(rows, c, c ? prev : r, r);        /* joined to the column before: the reset is a vertical */
        prev = r;
    }
}

void knob_frq(int32_t f, uint32_t *rows)
{
    int32_t st = f - 32, x = 8 + (st * 8 + (st < 0 ? -16 : 16)) / 32, c, k;   /* -32 to +31 over columns 0-16 */
    x = x < 0 ? 0 : x > 16 ? 16 : x;
    clear(rows);
    rows[8] = BIT(0) * 2 - 1;                   /* the axis */
    for (k = -24; k <= 24; k += 12) {           /* the octaves: ticks either side of it */
        c = 8 + k / 4;
        rows[7] |= BIT(c);
        rows[9] |= BIT(c);
    }
    for (c = x < 8 ? x : 8; c <= (x > 8 ? x : 8); c++)
        vline(rows, c, 6, 10);                  /* the bar from the centre */
    vline(rows, 8, 5, 11);                      /* the centre */
    vline(rows, x, 3, 13);                      /* the value */
}

/* #18's mockups, pixel for pixel. */
static const uint32_t mod_px[4][KNOB_PX] = {
    {0x00000, 0x00000, 0x00000, 0x00000, 0x00000, 0x00000, 0x08006, 0x18001, 0x08002, 0x08004, 0x1c007, 0x00000, 0x00000, 0x00000, 0x00000, 0x00000, 0x00000},   /* OFF */
    {0x00000, 0x00000, 0x00040, 0x00020, 0x01ff0, 0x00020, 0x08046, 0x18001, 0x08002, 0x08004, 0x1c007, 0x00000, 0x00000, 0x00000, 0x00000, 0x00000, 0x00000},   /* SYNC */
    {0x00000, 0x00000, 0x00000, 0x00000, 0x00000, 0x00000, 0x08006, 0x18001, 0x08002, 0x08004, 0x1c407, 0x00800, 0x01ff0, 0x00800, 0x00400, 0x00000, 0x00000},   /* XMOD */
    {0x00000, 0x00000, 0x00040, 0x00020, 0x01ff0, 0x00020, 0x08046, 0x18001, 0x08002, 0x08004, 0x1c407, 0x00800, 0x01ff0, 0x00800, 0x00400, 0x00000, 0x00000},   /* BOTH */
};
static const uint32_t noise_px[3][KNOB_PX] = {
    {0x00000, 0x00000, 0x00000, 0x00000, 0x00000, 0x00000, 0x00000, 0x00000, 0x1ffff, 0x00000, 0x00000, 0x00000, 0x00000, 0x00000, 0x00000, 0x00000, 0x00000},   /* OFF */
    {0x00000, 0x00000, 0x00000, 0x00000, 0x00000, 0x00e00, 0x18ac3, 0x0ebc3, 0x0eb7f, 0x03b7f, 0x00378, 0x00060, 0x00000, 0x00000, 0x00000, 0x00000, 0x00000},   /* LO */
    {0x00000, 0x00600, 0x00e00, 0x00a03, 0x18ac3, 0x08bc3, 0x0eb43, 0x0eb43, 0x0eb7f, 0x0fb7f, 0x01b7f, 0x00378, 0x00378, 0x00378, 0x00060, 0x00060, 0x00000},   /* HI */
};

void knob_mod(int32_t m, uint32_t *rows)
{
    int r;
    m = m < 0 ? 0 : m > 3 ? 3 : m;
    for (r = 0; r < KNOB_PX; r++)
        rows[r] = mod_px[m][r];
}

void knob_noise(int32_t n, uint32_t *rows)
{
    int r;
    n = n < 0 ? 0 : n > 2 ? 2 : n;
    for (r = 0; r < KNOB_PX; r++)
        rows[r] = noise_px[n][r];
}

void knob_mix(int32_t m, uint32_t *rows)
{
    static const uint32_t letters[5] = {0x07038, 0x04028, 0x06028, 0x04028, 0x04038};   /* F and O */
    static const int top[3] = {10, 6, 1};       /* each fader's top row: OFF, -3 dB, ON */
    int r;
    m = m < 0 ? 0 : m > 8 ? 8 : m;
    clear(rows);
    for (r = top[m / 3]; r < 10; r++)
        rows[r] |= 0x07800;                     /* FUND, columns 2-5 */
    for (r = top[m % 3]; r < 10; r++)
        rows[r] |= 0x0003c;                     /* OCT, columns 11-14 */
    rows[10] = 0x0fc7e;                         /* their bases */
    for (r = 0; r < 5; r++)
        rows[12 + r] = letters[r];
}
