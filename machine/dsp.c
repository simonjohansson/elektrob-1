/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "dsp.h"

void dsp_vco_set(struct dsp_vco *v, uint32_t inc)
{
    uint32_t i = inc, k = 0;
    while (i >= 1u << 16) {                     /* at most 16 shifts, once a block */
        i >>= 1;
        k++;
    }
    v->inc = inc;
    v->shift = k;
    v->rinc = i ? (1u << 30) / i : 0;
}

/* The residuals of a kernel two samples wide, x the time from the event in samples:
 * jump:  before (x + 1)^2 / 2, after -(1 - x)^2 / 2;  slope: before (x + 1)^3 / 6, after (1 - x)^3 / 6.
 * The previous sample is 1 - t before the event (x + 1 = t), the current one t after. |h| <= 65536 and
 * |m| <= 32768 (DSP_INC_MAX) keep every product inside 32 bits. */
void dsp_blep(int32_t *prev, int32_t *cur, int32_t t, int32_t h, int32_t m)
{
    int32_t a = t >> 1, b = 32768 - a;          /* t and 1 - t, Q15 */
    int32_t a2 = (a * a) >> 15, b2 = (b * b) >> 15;
    *prev += (h * a2) >> 16;
    *cur -= (h * b2) >> 16;
    if (m) {
        int32_t a3 = (a2 * a) >> 15, b3 = (b2 * b) >> 15;
        *prev += (((m * a3) >> 15) * 10923) >> 16;     /* / 6 */
        *cur += (((m * b3) >> 15) * 10923) >> 16;
    }
}

void dsp_saw_type(struct dsp_osc *o, uint32_t inc, int32_t g, int32_t *out, int n)
{
    int32_t prev = o->prev, i, t, y, s;
    int32_t dc = (32768 - g) >> 2;              /* the mean, (1 - g) / 4 */
    int32_t ds, kink;
    if (inc > DSP_INC_MAX)
        inc = DSP_INC_MAX;
    dsp_vco_set(&o->vco, inc);
    ds = (int32_t)(inc >> 16);                  /* the ramp's rise a sample */
    kink = ((32768 - g) * ds) >> 15;            /* the slope's change where s = 0: (1 - g) ds */
    for (i = 0; i < n; i++) {
        int32_t reset = dsp_vco_step(&o->vco);
        s = dsp_saw(o->vco.phase);
        y = (s > 0 ? s : (g * s) >> 15) - dc;
        if (reset >= 0)                         /* from 1 to -g, and the slope from ds to g ds */
            dsp_blep(&prev, &y, reset, -(32768 + g), -kink);
        if ((t = dsp_vco_crossed(&o->vco, 0x80000000u)) >= 0)
            dsp_blep(&prev, &y, t, 0, kink);
        out[i] = prev;
        prev = y;
    }
    o->prev = prev;
}

void dsp_pulse_type(struct dsp_osc *o, uint32_t inc, uint32_t width, int32_t *out, int n)
{
    int32_t prev = o->prev, i, t, y;
    int32_t dc = (int32_t)(width >> 16) - 32768;    /* the mean, 2 width - 1 */
    if (inc > DSP_INC_MAX)
        inc = DSP_INC_MAX;
    dsp_vco_set(&o->vco, inc);
    for (i = 0; i < n; i++) {
        int32_t reset = dsp_vco_step(&o->vco);
        y = (o->vco.phase < width ? 32767 : -32768) - dc;
        if (reset >= 0)                             /* up at the reset */
            dsp_blep(&prev, &y, reset, 65536, 0);
        if ((t = dsp_vco_crossed(&o->vco, width)) >= 0)
            dsp_blep(&prev, &y, t, -65536, 0);       /* down at the width */
        out[i] = prev;
        prev = y;
    }
    o->prev = prev;
}
