/* SPDX-License-Identifier: GPL-2.0-or-later */
/* ob1: the Oberheim OB-1's oscillator section as an SRC machine on core 3.0. For now one VCO (machine/dsp.c)
 * at the trig's note and TUNE, its wave type and WAVE on knob C; the track's filter, envelope, level, LFOs
 * and sends work on it as on a sample.
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
#define K_WAV1      2                   /* BR's: its range, 0-127, and its knob as they are */
#define K_LEV       7

/* ---- the sound (ev_render_voices: the render, interrupt level) ----------------------------------- */

static struct dsp_osc vco1[8];             /* free-running, as the OB-1's VCOs: never reset */

/* WAV1 0-63: saw type, triangle (0) to saw (63), as g -1 to +1, Q15. */
static int32_t wave_g(int32_t w)
{
    return -32768 + (w << 16) / 63;
}

/* WAV1 64-127: pulse type, from the narrowest pulse (64) to the square (127), as the OB-1's WAVE pot
 * turned clockwise ends on the square (trimmed to 50 %). shortcut: the narrowest width is a guess, 10 %;
 * set it from a real OB-1's sweep (#15). */
#define PW_MIN  429496730u                         /* 10 % of 2^32 */
static uint32_t wave_width(int32_t w)
{
    return PW_MIN + (uint32_t)(w - 64) * ((0x80000000u - PW_MIN) / 63);
}

void ob1_render(int32_t *blocks)
{
    int t;
    for (t = 0; t < 8; t++) {
        const volatile int16_t *p = &fw_voice_params[53 * t];
        int32_t pitch, w, lev, gain, i, x[32];
        uint32_t inc;
        if (fw_render_machine[t] != OB1_ID)
            continue;
        /* the note and TUNE as the playback takes a sample's: the pitch table at 1 for note 60, TUNE 0 */
        pitch = ((int32_t)p[K_TUNE] - 0x4000) * 256 + fw_voice_note[t] + (3 << 16);
        pitch = pitch < 0 ? 0 : pitch > (87 << 16) ? 87 << 16 : pitch;
        inc = (23410327u >> 13) * (fw_pitch_tab[pitch / 384] >> 16);   /* 261.63 Hz at 48 kHz, times it */
        w = (p[K_WAV1] >> 8) & 0x7f;
        lev = (p[K_LEV] >> 8) & 0x7f;
        gain = lev * lev;                                              /* LEV's law, as a sample's: Q14 */
        if (w < 64)
            dsp_saw_type(&vco1[t], inc, wave_g(w), x, 32);
        else
            dsp_pulse_type(&vco1[t], inc, wave_width(w), x, 32);
        for (i = 0; i < 32; i++)
            blocks[32 * t + i] = (x[i] * gain) << 2;                   /* Q15 x Q14 -> Q31 */
    }
}

/* ---- the page (machine-pages draws it) --------------------------------------------------------- */

/* WAV1's text: S0-S63 for the saw type, P0-P63 for the pulse type. */
static int32_t wave_text(char *buf, int32_t value, int32_t ctx, int32_t machine)
{
    int32_t v = (value >> 8) & 0x7f, n = 1;
    (void)ctx;
    (void)machine;
    buf[0] = v < 64 ? 'S' : 'P';
    v &= 63;
    if (v >= 10)
        buf[n++] = (char)('0' + v / 10);
    buf[n++] = (char)('0' + v % 10);
    buf[n] = 0;
    return 1;
}

static const struct cm_ui ob1_page = {
    .abi = cm_ui_v3,
    .page_from = 3,                                     /* SLICE's page, as every added machine gets */
    .knob = {
        [1] = { .flags = CM_HIDDEN },                   /* PLAY, SAMP, SLICE, LEN and GRID: a sample's */
        [K_WAV1] = { .name = "WAV1", .lname = "VCO1 wave", .fmt = wave_text },
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
