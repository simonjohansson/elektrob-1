/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "dsp.h"

/* A parabola, then its square nudged in (within 0.1 %). */
int32_t dsp_sine(uint32_t ph)
{
    int32_t x = (int32_t)ph >> 16, a = x < 0 ? -x : x, y, z;
    y = (x * (32768 - a)) >> 13;
    z = y < 0 ? -y : y;
    y += (7373 * (((y * z) >> 15) - y)) >> 15;
    return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
}

int32_t dsp_fold(int32_t y, int32_t shape)
{
    int32_t v = y + ((y * shape) >> 4);
    while (v > 32767 || v < -32767)
        v = v > 0 ? 65534 - v : -65534 - v;
    return v;
}

void dsp_render(uint32_t *phase, uint32_t inc, int32_t shape, int32_t gain, int32_t *out, int n)
{
    uint32_t ph = *phase;
    int i;
    for (i = 0; i < n; i++) {
        out[i] = (dsp_fold(dsp_sine(ph), shape) * gain) << 2;      /* Q15 x Q14 -> Q31 */
        ph += inc;
    }
    *phase = ph;
}
