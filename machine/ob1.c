/* SPDX-License-Identifier: GPL-2.0-or-later */
/* ob1: the Oberheim OB-1's oscillator section as an SRC machine on core 3.0. For now it still plays the
 * sine example's folded sine (machine/dsp.c) at the trig's note and TUNE; the track's filter, envelope,
 * level, LFOs and sends work on it as on a sample.
 *
 * It names no firmware address: the locations are core 3.0's exports (fw_*), and its page is drawn by the
 * machine-pages mod from the description below. So its mod.json's 1.54 port is empty.
 *
 * Machine 120: ids 120-127 are for examples and experiments; it takes one of its own before it is shared
 * (docs/ADAPTING.md, "SRC machines"). */
#include "digitakt-mk1/core3.h"
#include "dsp.h"

#define OB1_ID      120
#define K_TUNE      0                   /* the SRC page's knobs, A-H: SLICE's parameters */
#define K_SHAPE     2                   /* BR's: its range, 0-127, and its knob as they are */
#define K_LEV       7

/* ---- the sound (ev_render_voices: the render, interrupt level) ----------------------------------- */

static uint32_t phase[8];                  /* free-running, as the OB-1's VCOs: never reset */

void ob1_render(int32_t *blocks)
{
    int t;
    for (t = 0; t < 8; t++) {
        const volatile int16_t *p = &fw_voice_params[53 * t];
        int32_t pitch, shape, lev, gain;
        uint32_t inc;
        if (fw_render_machine[t] != OB1_ID)
            continue;
        /* the note and TUNE as the playback takes a sample's: the pitch table at 1 for note 60, TUNE 0 */
        pitch = ((int32_t)p[K_TUNE] - 0x4000) * 256 + fw_voice_note[t] + (3 << 16);
        pitch = pitch < 0 ? 0 : pitch > (87 << 16) ? 87 << 16 : pitch;
        inc = (23410327u >> 13) * (fw_pitch_tab[pitch / 384] >> 16);   /* 261.63 Hz at 48 kHz, times it */
        shape = (p[K_SHAPE] >> 8) & 0x7f;
        lev = (p[K_LEV] >> 8) & 0x7f;
        gain = lev * lev;                                              /* LEV's law, as a sample's: Q14 */
        dsp_render(&phase[t], inc, shape, gain, &blocks[32 * t], 32);
    }
}

/* ---- the page (machine-pages draws it) --------------------------------------------------------- */

static int32_t shape_text(char *buf, int32_t value, int32_t ctx, int32_t machine)
{
    int32_t v = (value >> 8) & 0x7f, n = 0;
    char d[3];
    (void)ctx;
    (void)machine;
    if (!v) {
        buf[0] = 'S'; buf[1] = 'I'; buf[2] = 'N'; buf[3] = 0;          /* 0: a plain sine */
        return 1;
    }
    do {
        d[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    for (v = 0; n; v++)
        buf[v] = d[--n];
    buf[v] = 0;
    return 1;
}

static const struct cm_ui ob1_page = {
    .abi = cm_ui_v3,
    .page_from = 3,                                     /* SLICE's page, as every added machine gets */
    .knob = {
        [1] = { .flags = CM_HIDDEN },                   /* PLAY, SAMP, SLICE, LEN and GRID: a sample's */
        [K_SHAPE] = { .name = "SHAP", .lname = "Shape", .fmt = shape_text },
        [3] = { .flags = CM_HIDDEN },
        [4] = { .flags = CM_HIDDEN },
        [5] = { .flags = CM_HIDDEN },
        [6] = { .flags = CM_HIDDEN },
    },
};

/* An 11 x 7 icon, as the stock ones: a word a column, the top row in bit 25, pixels then mask. A sawtooth. */
static const uint32_t ob1_px[11] = {
    0x80000000, 0x40000000, 0x20000000, 0x10000000, 0x08000000, 0x04000000,
    0xfe000000, 0x40000000, 0x20000000, 0x10000000, 0x08000000,
};
static const uint32_t ob1_mask[11] = {
    0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000,
    0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000,
};
static const struct {
    const void *vt;
    int32_t width, height, one;
    const uint32_t *px, *mask;
    int32_t zero;
} ob1_icon = { fw_bitmap_vt, 11, 7, 1, ob1_px, ob1_mask, 0 };

/* params 3 and render its own id: SLICE's eight parameters (their defaults, MIDI, the LFOs), and an empty
 * voice window that ob1_render fills. */
const struct cm_machine ob1_machine = {
    OB1_ID, "OB-1", "OB-1", &ob1_icon, 3, OB1_ID, CM_UI_TAG, &ob1_page,
};
