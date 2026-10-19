/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef DSP_OUTPUT_H
#define DSP_OUTPUT_H
#include "dsp.h"

static inline __attribute__((always_inline)) uint32_t noise_next(uint32_t x)
{
    return x * 1664525u + 1013904223u;
}

/* ob1_dsp_voice_out's loop, compiled with and without the noise, and for VCO1's two sources alone (four: false)
 * or all four: fewer live values, fewer spilled to the stack. */
static inline __attribute__((always_inline))
void voice_out(struct dsp_hpf *h, const int32_t *const src[4], const int32_t gain[4], uint32_t *noise,
                   int32_t ng, int32_t *out, int n, int with_noise, int four, int32_t *smooth)
{
    const int32_t *a = src[0], *b = src[1], *c = src[2], *d = src[3];
    int32_t ga = gain[0], gb = gain[1], gc = gain[2], gd = gain[3], lp = h->lp, x, i;
    uint32_t z = *noise;
    int32_t prev = smooth ? smooth[0] : 0, older = smooth ? smooth[1] : 0;
    for (i = 0; i < n; i++) {
        x = a[i] * ga + b[i] * gb;                              /* |src| < 2^16, gain <= 2^12: < 2^30 */
        if (four)
            x += c[i] * gc + d[i] * gd;
        if (with_noise) {
            z = noise_next(z);
            int32_t raw = (int32_t)z >> 16;
            x += raw * ng;
        }
        x >>= 14;                               /* Q12 to Q15, and a quarter */
        lp += (((x * 256) - lp) * 47) >> 15;     /* the low-pass the high-pass takes off: 2 pi 11 / 48000 */
        x -= lp >> 8;
        if (smooth) {
            x = x > 32767 ? 32767 : x < -32768 ? -32768 : x;
            out[i] = ((x + 2 * prev + older) >> 2) * 65536;
            older = prev;
            prev = x;
        } else
            out[i] = x > 32767 ? 0x7fffffff : x < -32768 ? (int32_t)0x80000000 : x * 65536;
    }
    h->lp = lp;
    *noise = z;
    if (smooth) {
        smooth[0] = prev;
        smooth[1] = older;
    }
}

static inline __attribute__((always_inline))
void voice_output(struct dsp_hpf *h, const int32_t *const src[4], const int32_t gain[4], uint32_t *noise,
                   int32_t noise_gain, int32_t *out, int n, int32_t *smooth)
{
    int four = gain[2] || gain[3];
    if (noise_gain && four)
        voice_out(h, src, gain, noise, noise_gain, out, n, 1, 1, smooth);
    else if (noise_gain)
        voice_out(h, src, gain, noise, noise_gain, out, n, 1, 0, smooth);
    else if (four)
        voice_out(h, src, gain, noise, 0, out, n, 0, 1, smooth);
    else
        voice_out(h, src, gain, noise, 0, out, n, 0, 0, smooth);
}

#endif
