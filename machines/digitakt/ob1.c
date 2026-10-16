/* SPDX-License-Identifier: GPL-2.0-or-later */
/* ob1: the Oberheim OB-1's oscillator section as an SRC machine on core 3.0: two VCOs (dsp/dsp.c), VCO1 at
 * the trig's note and TUNE, VCO2 FRQ2 semitones from it, each with its wave type and WAVE and its FUND and
 * OCT mix, VCO2 synced to VCO1 and VCO1 cross-modulated by VCO2 on MOD, and noise; the track's filter,
 * envelope, level, LFOs and sends work on it as on a sample.
 *
 * It names no firmware address: the locations are core 3.0's exports (fw_*), and its page is drawn by the
 * machine-pages mod from the description below. So its mod.json's 1.54 port is empty.
 *
 * Machine 69, claimed as the resource machine:69: kits store it, so it is fixed for good (docs/ADAPTING.md,
 * "SRC machines"). */
#include "digitakt-mk1/core3.h"
#include "../../dsp/voice.h"
#include "knobs.h"

#define OB1_ID      69
#define K_TUNE      0                   /* the SRC page's knobs, A-H: SLICE's parameters */
#define K_FRQ2      1                   /* PLAY's slot, 0-63: VCO2, -32 to +31 semitones from VCO1 */
#define K_WAV1      2                   /* BR's: its range, 0-127, and its knob as they are */
#define K_WAV2      3                   /* SAMP's slot, 0-127, not opening the sample list */
#define K_MOD       4                   /* SLICE's slot, 0-127 as 4 zones: OFF, SYNC, XMOD, BOTH */
#define K_MIX1      5                   /* LEN's slot, 0-127 as 9 zones: VCO1 FUND x OCT */
#define K_MIX2      6                   /* GRID's slot, 0-127 as 9 zones: VCO2's */
#define K_NOIS      7                   /* LEV's slot, 0-127 as 3 zones: NOISE OFF, -3 dB, ON */
#define ID_BR       134                 /* BR's parameter id (SLICE's knob C): a plain knob, for a slot whose own turns oddly */

/* ---- the sound (ev_render_voices: the render, interrupt level) ----------------------------------- */

static struct ob1_voice voice[8];            /* set up on its first block */
static uint8_t ready[8];

/* A pitch (semitones << 16, the pitch table at 1 for 3 << 16) as a VCO's step. */
static uint32_t pitch_inc(int32_t pitch)
{
    pitch = pitch < 0 ? 0 : pitch > (87 << 16) ? 87 << 16 : pitch;
    return (23410327u >> 13) * (fw_pitch_tab[pitch / 384] >> 16);  /* 261.63 Hz at 48 kHz, times it */
}

/* A knob's value, 8.8, as 0-max. */
static int32_t knob(const volatile int16_t *p, int k, int32_t max)
{
    int32_t v = p[k] >> 8;
    return v < 0 ? 0 : v > max ? max : v;
}

/* A switch's knob value, 8.8, 0-127 in n equal zones (a detent a setting was too fiddly to turn), as its
 * setting, 0 to n - 1; ZONE_AT(s, n) is setting s's middle. */
static int32_t zone(int32_t value, int32_t n)
{
    int32_t v = value >> 8;
    return (v < 0 ? 0 : v > 127 ? 127 : v) * n >> 7;
}
#define ZONE_AT(s, n)   (((s) * 128 + 64) / (n))

/* MIX1's and MIX2's value as its setting, FUND x 3 + OCT (0 OFF, 1 -3 dB, 2 ON): FUND rises first, then OCT
 * with FUND ON, then the octave-led settings. */
static int32_t mix(int32_t value)
{
    static const int8_t order[9] = {0, 3, 6, 7, 8, 4, 5, 1, 2};
    return order[zone(value, 9)];
}

/* A voice the AMP envelope has silenced and no trig starts: inaudible after the AMP page, so it is not
 * rendered. Its VCOs then stand still, which a free-running VCO's phase hides. The envelope's level is 0
 * only then: the output goes about as its cube root (measured in digiemu), so a release still sounds at
 * -32 dB with a level of 2^15, and at -45 dB with 390. */
static int silent(int t)
{
    return !(fw_voice_start & (1u << t)) && fw_amp_env[3 * t + 1] == 0;
}

void ob1_render(int32_t *blocks)
{
    int t;
    for (t = 0; t < 8; t++) {
        const volatile int16_t *p = &fw_voice_params[53 * t];
        struct ob1_knobs k;
        int32_t pitch, i;
        if (fw_render_machine[t] != OB1_ID)
            continue;
        if (silent(t)) {
            for (i = 0; i < 32; i++)
                blocks[32 * t + i] = 0;
            continue;
        }
        if (!ready[t]) {
            ob1_voice_init(&voice[t], t + 1);
            ready[t] = 1;
        }
        k.wav1 = knob(p, K_WAV1, 127);
        k.wav2 = knob(p, K_WAV2, 127);
        k.mix1 = mix(p[K_MIX1]);
        k.mix2 = mix(p[K_MIX2]);
        k.mod = zone(p[K_MOD], 4);
        k.nois = zone(p[K_NOIS], 3);
        /* the note and TUNE as the playback takes a sample's: the pitch table at 1 for note 60, TUNE 0 */
        pitch = ((int32_t)p[K_TUNE] - 0x4000) * 256 + fw_voice_note[t] + (3 << 16);
        ob1_voice_render(&voice[t], &k, pitch_inc(pitch), pitch_inc(pitch + ((knob(p, K_FRQ2, 63) - 32) << 16)),
                         &blocks[32 * t]);
    }
}

/* ---- the page (machine-pages draws it) --------------------------------------------------------- */

/* WAV1's and WAV2's text: S0-S63 for the saw type, P0-P63 for the pulse type. */
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

/* FRQ2's text: semitones from VCO1, "+7", "-12". */
static int32_t frq_text(char *buf, int32_t value, int32_t ctx, int32_t machine)
{
    int32_t v = (value >> 8) - 32, n = 1;
    (void)ctx;
    (void)machine;
    buf[0] = v < 0 ? '-' : '+';
    v = v < 0 ? -v : v;
    if (v >= 10)
        buf[n++] = (char)('0' + v / 10);
    buf[n++] = (char)('0' + v % 10);
    buf[n] = 0;
    return 1;
}

/* NOISE's text. */
static int32_t noise_text(char *buf, int32_t value, int32_t ctx, int32_t machine)
{
    static const char *const word[3] = {"OFF", "-3DB", "ON"};
    int32_t n = 0;
    const char *w;
    (void)ctx;
    (void)machine;
    for (w = word[zone(value, 3)]; *w; )
        buf[n++] = *w++;
    buf[n] = 0;
    return 1;
}

/* MOD's text. */
static int32_t mod_text(char *buf, int32_t value, int32_t ctx, int32_t machine)
{
    static const char *const word[4] = {"OFF", "SYNC", "XMOD", "BOTH"};
    int32_t n = 0;
    const char *w;
    (void)ctx;
    (void)machine;
    for (w = word[zone(value, 4)]; *w; )
        buf[n++] = *w++;
    buf[n] = 0;
    return 1;
}

/* MIX1's and MIX2's text: FUND and OCT as - (OFF), -3 (-3 dB) or ON, "ON/-"; the pop-up spells it out, in
 * its 15 characters. */
static int32_t mix_text(char *buf, int32_t value, int32_t ctx, int32_t machine)
{
    static const char *const lvl[3] = {"-", "-3", "ON"};
    static const char *const word[3] = {"OFF", "-3", "ON"};
    int32_t v = mix(value), n = 0;
    const char *w;
    (void)machine;
    if (!ctx) {
        for (w = lvl[v / 3]; *w; )
            buf[n++] = *w++;
        buf[n++] = '/';
        for (w = lvl[v % 3]; *w; )
            buf[n++] = *w++;
        buf[n] = 0;
        return 1;
    }
    for (w = "FUND "; *w; )
        buf[n++] = *w++;
    for (w = word[v / 3]; *w; )
        buf[n++] = *w++;
    for (w = " OCT "; *w; )
        buf[n++] = *w++;
    for (w = word[v % 3]; *w; )
        buf[n++] = *w++;
    buf[n] = 0;
    return 1;
}

/* ---- the knob graphics (#18): knobs.c's rows, drawn into the box x + 1 to x + 17, y to y + 16
 * (y = 0 the screen's bottom row), its top row first ---------------------------------------------------- */
typedef void (*rect_fn)(void *bmp, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t colour);
#define FILLRECT ((rect_fn)fw_fillrect)

static int32_t draw_rows(void *bmp, int32_t x, int32_t y, const uint32_t *rows)
{
    int32_t r, c, c0;
    FILLRECT(bmp, x + 1, y, x + KNOB_PX, y + KNOB_PX - 1, 0);
    for (r = 0; r < KNOB_PX; r++)
        for (c = 0; c < KNOB_PX; ) {                       /* each run of lit pixels: one rectangle */
            if (!(rows[r] >> (KNOB_PX - 1 - c) & 1)) {
                c++;
                continue;
            }
            for (c0 = c; c < KNOB_PX && rows[r] >> (KNOB_PX - 1 - c) & 1; c++)
                ;
            FILLRECT(bmp, x + 1 + c0, y + KNOB_PX - 1 - r, x + c, y + KNOB_PX - 1 - r, 1);
        }
    return 1;
}

static int32_t draw_wave(void *bmp, int32_t x, int32_t y, int32_t value, int32_t flag)
{
    uint32_t rows[KNOB_PX];
    int32_t v = value >> 8;
    (void)flag;
    knob_wave(v < 0 ? 0 : v > 127 ? 127 : v, rows);
    return draw_rows(bmp, x, y, rows);
}

static int32_t draw_frq(void *bmp, int32_t x, int32_t y, int32_t value, int32_t flag)
{
    uint32_t rows[KNOB_PX];
    int32_t v = value >> 8;
    (void)flag;
    knob_frq(v < 0 ? 0 : v > 63 ? 63 : v, rows);
    return draw_rows(bmp, x, y, rows);
}

static int32_t draw_mod(void *bmp, int32_t x, int32_t y, int32_t value, int32_t flag)
{
    uint32_t rows[KNOB_PX];
    (void)flag;
    knob_mod(zone(value, 4), rows);
    return draw_rows(bmp, x, y, rows);
}

static int32_t draw_mix(void *bmp, int32_t x, int32_t y, int32_t value, int32_t flag)
{
    uint32_t rows[KNOB_PX];
    (void)flag;
    knob_mix(mix(value), rows);
    return draw_rows(bmp, x, y, rows);
}

static int32_t draw_noise(void *bmp, int32_t x, int32_t y, int32_t value, int32_t flag)
{
    uint32_t rows[KNOB_PX];
    (void)flag;
    knob_noise(zone(value, 3), rows);
    return draw_rows(bmp, x, y, rows);
}

/* A knob's value set in the sound, which is what is saved and shown, and in the engine (fw_set_param alone
 * leaves the sound as it was). */
static void set(uint8_t *sound, int32_t track, int k, int32_t v)
{
    *(uint16_t *)(sound + FW_SOUND_PARAM(k)) = (uint16_t)(v << 8);
    fw_set_param(v << 8, track, 17 + k);
}

/* A track turns to OB-1: both VCOs saws in unison, VCO1's FUND ON, VCO2's at -3 dB, noise OFF, no sync or
 * cross mod. CM_DEFAULT alone does not
 * reach the slots on a switch (they keep SLICE's stock defaults: MIX1 0, all off, silent). */
static void ob1_switch(int32_t track, int32_t from, uint8_t *sound)
{
    (void)from;
    set(sound, track, K_FRQ2, 32);
    set(sound, track, K_WAV1, 63);
    set(sound, track, K_WAV2, 63);
    set(sound, track, K_MIX1, ZONE_AT(2, 9));                 /* ON/- */
    set(sound, track, K_MIX2, ZONE_AT(1, 9));                 /* -3/- */
    set(sound, track, K_MOD, 0);
    set(sound, track, K_NOIS, 0);
}

static const struct cm_ui ob1_page = {
    .abi = cm_ui_v31,                                   /* machine-pages 1.1: knobs that draw */
    .page_from = 3,                                     /* SLICE's page, as every added machine gets */
    .on_switch = ob1_switch,
    .knob = {
        /* the slots whose own UI records turn oddly (PLAY, SAMP, LEN, GRID) borrow BR's */
        [K_FRQ2] = { .name = "FRQ2", .lname = "VCO2 freq", .look = ID_BR, .flags = CM_RANGE | CM_DEFAULT | CM_DRAW,
                     .min = 0, .max = 63 << 8, .def = 32 << 8, .fmt = frq_text, .draw = draw_frq },
        [K_WAV1] = { .name = "WAV1", .lname = "VCO1 wave", .flags = CM_DRAW, .fmt = wave_text, .draw = draw_wave },
        [K_WAV2] = { .name = "WAV2", .lname = "VCO2 wave", .look = ID_BR, .flags = CM_NOT_SAMPLE | CM_RANGE |
                     CM_DEFAULT | CM_DRAW, .min = 0, .max = 127 << 8, .def = 63 << 8, .fmt = wave_text,
                     .draw = draw_wave },
        [K_MOD] = { .name = "MOD", .lname = "Sync/cross mod", .look = ID_BR, .flags = CM_RANGE | CM_DEFAULT | CM_DRAW,
                    .min = 0, .max = 127 << 8, .def = 0, .fmt = mod_text, .draw = draw_mod },
        [K_MIX1] = { .name = "MIX1", .lname = "VCO1 mix", .look = ID_BR, .flags = CM_RANGE | CM_DEFAULT | CM_DRAW,
                     .min = 0, .max = 127 << 8, .def = ZONE_AT(2, 9) << 8, .fmt = mix_text, .draw = draw_mix },  /* ON/- */
        [K_MIX2] = { .name = "MIX2", .lname = "VCO2 mix", .look = ID_BR, .flags = CM_RANGE | CM_DEFAULT | CM_DRAW,
                     .min = 0, .max = 127 << 8, .def = ZONE_AT(1, 9) << 8, .fmt = mix_text, .draw = draw_mix },  /* -3/- */
        [K_NOIS] = { .name = "NOIS", .lname = "Noise", .look = ID_BR, .flags = CM_RANGE | CM_DEFAULT | CM_DRAW,
                     .min = 0, .max = 127 << 8, .def = 0, .fmt = noise_text, .draw = draw_noise },
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
