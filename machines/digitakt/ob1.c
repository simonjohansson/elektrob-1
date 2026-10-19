/* SPDX-License-Identifier: GPL-2.0-or-later */
/* OB-1's oscillator section: independent pitch, family, shape and output controls for two VCOs.
 * The track's filter, envelope, LFOs and sends retain their stock paths. Machine ID 69 is stored
 * in sounds; the parameter mapping belongs to this two-page integration. */
#include "digitakt-mk1/core3.h"
#include "../../dsp/voice.h"
#include "knobs.h"
#include "pitch.h"
#include "notes.h"

#define OB1_ID 69
#define ID_BR 134

enum {
    K_FRQ1, K_FIN1, K_TYP1, K_WAV1, K_FRQ2, K_FIN2, K_TYP2, K_WAV2,
    K_FND1, K_OCT1, K_XMOD, K_FND2, K_OCT2, K_SYNC, K_NOIS, K_COUNT
};

static const struct cm_ui32 ob1_page;
static struct ob1_voice voice[8];            /* set up on its first block */
static volatile uint8_t ready[8];

int32_t ob1_midi_cc_parameter(int32_t track, int32_t machine, int32_t cc)
{
    if (track < 0 || track >= 8 || machine != OB1_ID)
        return 0;
    /* Defer the full reset to the audio callback; CC 9 has no stock assignment. */
    if (cc == 9)
        ready[track] = 0;
    return cc >= 24 && cc <= 30 ? 164 + cc - 24 : 0;
}

/* ---- the sound (ev_render_voices: the render, interrupt level) ----------------------------------- */

/* A knob's value, 8.8, as 0-max. */
static int32_t knob(int32_t value, int32_t max)
{
    int32_t v = value >> 8;
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

/* Fine tune is Q16 semitones: exact zero at 64, with +/-50 cents at the endpoints. */
static int32_t fine(int32_t value)
{
    int32_t v = (value < 0 ? 0 : value > 0x7f00 ? 0x7f00 : value) - 0x4000;
    return v <= 0 ? v * 2 : v * 128 / 63;
}

/* TYPE spans 0–1; fractional modulation switches at the midpoint. */
static int32_t family(int32_t value)
{
    return value >= 0x80;
}

static int32_t wave(int32_t type, int32_t position)
{
    return family(type) * 64 + knob(position, 63);
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
        int32_t p[K_COUNT];
        struct ob1_knobs k;
        int32_t pitch1, pitch2, i;
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
        for (i = 0; i < 8; i++)
            p[i] = fw_voice_params[53 * t + i];
        for (i = 8; i < K_COUNT; i++)
            p[i] = cm_src_value(t, i);
        k.wav1 = wave(p[K_TYP1], p[K_WAV1]);
        k.wav2 = wave(p[K_TYP2], p[K_WAV2]);
        k.mix1 = zone(p[K_FND1], 3) * 3 + zone(p[K_OCT1], 3);
        k.mix2 = zone(p[K_FND2], 3) * 3 + zone(p[K_OCT2], 3);
        k.mod = zone(p[K_SYNC], 2) * DSP_SYNC | zone(p[K_XMOD], 2) * DSP_XMOD;
        k.nois = zone(p[K_NOIS], 3);
        /* Each oscillator offsets the played note; the firmware pitch table's unity is at 63. */
        pitch1 = fw_voice_note[t] + (3 << 16) + (knob(p[K_FRQ1], 63) - 32) * 65536 + fine(p[K_FIN1]);
        pitch2 = fw_voice_note[t] + (3 << 16) + (knob(p[K_FRQ2], 63) - 32) * 65536 + fine(p[K_FIN2]);
        ob1_voice_render(&voice[t], &k, ob1_pitch_inc(pitch1, fw_pitch_tab),
                         ob1_pitch_inc(pitch2, fw_pitch_tab), &blocks[32 * t]);
    }
}

/* ---- the page (machine-pages draws it) --------------------------------------------------------- */

/* Coarse pitch: semitones from the played note. */
static int32_t frq_text(char *buf, int32_t value, int32_t ctx, int32_t machine)
{
    int32_t v = knob(value, 63) - 32, n = 1;
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

static int32_t fine_text(char *buf, int32_t value, int32_t ctx, int32_t machine)
{
    int32_t cents = fine(value), n = 1;
    int32_t v = (cents * 1000 + (cents < 0 ? -32768 : 32768)) / 65536;
    (void)ctx;
    (void)machine;
    buf[0] = v < 0 ? '-' : '+';
    v = v < 0 ? -v : v;
    if (v >= 100)
        buf[n++] = (char)('0' + v / 100);
    buf[n++] = (char)('0' + v / 10 % 10);
    buf[n++] = '.';
    buf[n++] = (char)('0' + v % 10);
    buf[n] = 0;
    return 1;
}

static int32_t text(char *buf, const char *word)
{
    while (*word)
        *buf++ = *word++;
    *buf = 0;
    return 1;
}

static int32_t type_text(char *buf, int32_t value, int32_t ctx, int32_t machine)
{
    (void)ctx;
    (void)machine;
    return text(buf, family(value) ? "PULS" : "SAW");
}

static int32_t switch_text(char *buf, int32_t value, int32_t ctx, int32_t machine)
{
    (void)ctx;
    (void)machine;
    return text(buf, zone(value, 2) ? "ON" : "OFF");
}

static int32_t level_text(char *buf, int32_t value, int32_t ctx, int32_t machine)
{
    static const char *const word[3] = {"OFF", "-3DB", "ON"};
    (void)ctx;
    (void)machine;
    return text(buf, word[zone(value, 3)]);
}

/* ---- the knob graphics: knobs.c's rows, drawn into the box x + 1 to x + 17, y to y + 16
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

static int32_t draw_type(void *bmp, int32_t x, int32_t y, int32_t value, int32_t flag)
{
    uint32_t rows[KNOB_PX];
    (void)flag;
    ob1_knob_wave(family(value) ? 127 : 63, rows);
    return draw_rows(bmp, x, y, rows);
}

static int32_t draw_wave(void *bmp, int32_t x, int32_t y, int32_t value, int32_t flag)
{
    uint32_t rows[KNOB_PX];
    (void)flag;
    /* The frame callback supplies the family from the displayed sound, including held-step locks. */
    ob1_knob_wave(wave(0, value), rows);
    return draw_rows(bmp, x, y, rows);
}

static void *pointer_at(const void *object, int offset)
{
    return *(void *const *)((const uint8_t *)object + offset);
}

static int named(const void *view, const char *expected)
{
    void *const *vt = pointer_at(view, 0);
    const char *name = ((const char *const *)vt[-1])[1];
    while (*expected && *name == *expected)
        name++, expected++;
    return !*expected && !*name;
}

extern int32_t ob1_view_value(void *view, int32_t parameter, uint8_t *locked);

void ob1_draw(void *bmp, void *ctrl)
{
    void *view = 0, *end = (uint8_t *)ctrl + 20;
    void *node = pointer_at(ctrl, 28);
    /* A later modal view must retain the composed frame. */
    for (int count = 0; node != end && count < 32; count++, node = pointer_at(node, 0)) {
        void *candidate = pointer_at(node, 8);
        if (!((const uint8_t *)candidate)[21])
            continue;
        if (named(candidate, "14SamplePageView"))
            view = candidate;
        else if (view && !named(candidate, "14SamplerLedView") && !named(candidate, "12TempoLedView") &&
                 !named(candidate, "15PatternGridView") && !named(candidate, "12KeyboardView") &&
                 !named(candidate, "14HelpBubbleView"))
            return;
    }
    if (node != end || !view)
        return;
    uintptr_t *vt = pointer_at(view, 0);
    int32_t (*knob_id)(void *, int32_t) = (int32_t (*)(void *, int32_t))vt[39];
    if (knob_id(view, 2) != 134 || knob_id(view, 3) != 135 ||
        knob_id(view, 6) != 138 || knob_id(view, 7) != 139)
        return;
    void *parameter = (void *)((uintptr_t (*)(void *, int32_t, int32_t))vt[41])(view, 135, -1);
    if (!parameter)
        return;
    void *reference = pointer_at(parameter, 16);
    uint8_t *sound = reference ? pointer_at(reference, 16) : 0;
    if (!sound || sound[FW_SOUND_MACHINE] != OB1_ID)
        return;
    for (int oscillator = 0; oscillator < 2; oscillator++) {
        uint8_t locked;
        int32_t family = ob1_view_value(view, 134 + 4 * oscillator, &locked);
        int32_t position = ob1_view_value(view, 135 + 4 * oscillator, &locked);
        uint32_t rows[KNOB_PX];
        ob1_knob_wave(wave(family, position), rows);
        draw_rows(bmp, 102, 34 - 27 * oscillator, rows);
    }
}

static int32_t draw_frq(void *bmp, int32_t x, int32_t y, int32_t value, int32_t flag)
{
    uint32_t rows[KNOB_PX];
    int32_t v = value >> 8;
    (void)flag;
    ob1_knob_frq(v < 0 ? 0 : v > 63 ? 63 : v, rows);
    return draw_rows(bmp, x, y, rows);
}

static int32_t draw_xmod(void *bmp, int32_t x, int32_t y, int32_t value, int32_t flag)
{
    uint32_t rows[KNOB_PX];
    (void)flag;
    ob1_knob_mod(zone(value, 2) * DSP_XMOD, rows);
    return draw_rows(bmp, x, y, rows);
}

static int32_t draw_sync(void *bmp, int32_t x, int32_t y, int32_t value, int32_t flag)
{
    uint32_t rows[KNOB_PX];
    (void)flag;
    ob1_knob_mod(zone(value, 2) * DSP_SYNC, rows);
    return draw_rows(bmp, x, y, rows);
}

static int32_t draw_level(void *bmp, int32_t x, int32_t y, int32_t value, int32_t flag)
{
    uint32_t rows[KNOB_PX];
    (void)flag;
    int32_t top = 12 - 6 * zone(value, 3);
    /* A vertical slot, with ON above the centre -3 dB position and OFF below it. */
    for (int r = 0; r < KNOB_PX; r++)
        rows[r] = r == 0 || r == 16 ? 0x00fe0 : 0x00820;
    for (int r = top; r < top + 5; r++)
        rows[r] |= 0x01ff0;
    rows[top + 2] &= ~0x00fe0u;               /* the handle's grip */
    rows[2] |= 7;
    rows[8] |= 7;
    rows[14] |= 7;
    return draw_rows(bmp, x, y, rows);
}

static void ob1_switch(int32_t track, int32_t from, uint8_t *sound)
{
    (void)from;
    ob1_notes_clear(track);
    /* Detune the fresh patch while retaining zero as the control's neutral default. */
    *(uint16_t *)(sound + 20 + 2 * 22) = 67 << 8;
    fw_set_param(67 << 8, track, 22);
}

static const struct cm_src_page ob1_pages[] = {
    { .title = "OB-1 OSC", .parameter = {K_FRQ1, K_FIN1, K_TYP1, K_WAV1, K_FRQ2, K_FIN2, K_TYP2, K_WAV2} },
    { .title = "OB-1 MIX", .parameter = {K_FND1, K_OCT1, K_XMOD, CM_SRC_BLANK, K_FND2, K_OCT2, K_SYNC, K_NOIS} },
};

#define CONTROL(label, long_name, maximum, initial, format, graphic, extra_flags) { \
    .name = label, .lname = long_name, .look = ID_BR, \
    .flags = CM_RANGE | CM_DEFAULT | (extra_flags), .max = (maximum) << 8, \
    .def = (initial) << 8, .fmt = format, .draw = graphic }

static const struct cm_ui32 ob1_page = {
    .base = {
        .abi = cm_ui_v32, .page_from = 3, .group = "OB-1", .on_switch = ob1_switch,
        .knob = {
            CONTROL("FRQ1", "VCO1 frequency", 63, 32, frq_text, draw_frq, CM_DRAW),
            CONTROL("FIN1", "VCO1 fine tune", 127, 64, fine_text, 0, 0),
            CONTROL("TYP1", "VCO1 family", 1, 0, type_text, draw_type, CM_DRAW),
            CONTROL("WAV1", "VCO1 shape", 63, 63, 0, draw_wave, CM_NOT_SAMPLE | CM_DRAW),
            CONTROL("FRQ2", "VCO2 frequency", 63, 32, frq_text, draw_frq, CM_DRAW),
            CONTROL("FIN2", "VCO2 fine tune", 127, 64, fine_text, 0, 0),
            CONTROL("TYP2", "VCO2 family", 1, 0, type_text, draw_type, CM_DRAW),
            CONTROL("WAV2", "VCO2 shape", 63, 63, 0, draw_wave, CM_DRAW),
        },
    },
    .extra = {
        CONTROL("FND1", "VCO1 fund level", 127, ZONE_AT(2, 3), level_text, draw_level, CM_DRAW),
        CONTROL("OCT1", "VCO1 sub octave", 127, 0, level_text, draw_level, CM_DRAW),
        CONTROL("XMOD", "Cross mod", 127, 0, switch_text, draw_xmod, CM_DRAW),
        CONTROL("FND2", "VCO2 fund level", 127, ZONE_AT(1, 3), level_text, draw_level, CM_DRAW),
        CONTROL("OCT2", "VCO2 sub octave", 127, 0, level_text, draw_level, CM_DRAW),
        CONTROL("SYNC", "VCO2 sync", 127, 0, switch_text, draw_sync, CM_DRAW),
        CONTROL("NOIS", "Noise level", 127, 0, level_text, draw_level, CM_DRAW),
    },
    .page_count = 2,
    .pages = ob1_pages,
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
    OB1_ID, "OB-1", "OB-1", &ob1_icon, 3, OB1_ID, CM_UI_TAG, &ob1_page.base,
};
