/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../machines/digitakt/ob1.c"

volatile uint8_t fw_render_machine[8];
volatile int16_t fw_voice_params[8 * 53];
volatile int32_t fw_voice_note[8], fw_amp_env[8 * 3];
volatile uint32_t fw_voice_start;
/* Sparse pitch-table entries exercise unison, octaves and fractional tuning. */
const uint32_t fw_pitch_tab[14849] = {
    [10750] = 536507626, [10752] = 1u << 29, [10753] = 537052647, [12800] = 1u << 30
};
const char fw_bitmap_vt[1], cm_ui_v32[1];
static int cleared_track;
static void *expected_view;
static int32_t ui_values[8];
static int ui_page, ui_reads;

void fw_fillrect(void *bmp, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t colour)
{
    uint8_t (*pixels)[128] = bmp;
    assert(x0 >= 0 && x1 < 128 && y0 >= 0 && y1 < 64);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            pixels[y][x] = (uint8_t)colour;
}

void ob1_notes_clear(int32_t track)
{
    cleared_track = track;
}

int32_t cm_src_value(int32_t track, int32_t parameter)
{
    int slot = parameter < 8 ? 17 + parameter : 46 + parameter - 8;
    return fw_voice_params[53 * track + slot - 17];
}

void fw_set_param(int32_t value, int32_t track, int32_t slot)
{
    fw_voice_params[53 * track + slot - 17] = value;
}

int32_t ob1_view_value(void *view, int32_t parameter, uint8_t *locked)
{
    assert(view == expected_view && parameter >= 132 && parameter < 140);
    *locked = 1;
    ui_reads++;
    return ui_values[parameter - 132];
}

static uint8_t ui_sound[162], parameter_object[32], sound_reference[32];

static int32_t view_knob(void *view, int32_t index)
{
    assert(view == expected_view && (index == 2 || index == 3 || index == 6 || index == 7));
    return (ui_page ? 164 : 132) + index;
}

static uintptr_t view_parameter(void *view, int32_t id, int32_t track)
{
    assert(view == expected_view && id == 135 && track == -1);
    return (uintptr_t)parameter_object;
}

static void put_pointer(void *object, int offset, const void *pointer)
{
    memcpy((uint8_t *)object + offset, &pointer, sizeof pointer);
}

static void waveform_pixels(uint8_t pixels[64][128], int oscillator, int shape)
{
    uint32_t rows[KNOB_PX];
    ob1_knob_wave(shape, rows);
    for (int r = 0; r < KNOB_PX; r++)
        for (int c = 0; c < KNOB_PX; c++)
            assert(pixels[34 - 27 * oscillator + 16 - r][103 + c] == ((rows[r] >> (16 - c)) & 1));
}

static void test_waveform_graphics(void)
{
    uint8_t controller[40] = {0}, view[32] = {0}, node[16] = {0};
    uint8_t modal[32] = {0}, modal_node[16] = {0};
    uintptr_t storage[43] = {0}, modal_storage[2];
    const char *typeinfo[] = {0, "14SamplePageView"};
    const char *modal_typeinfo[] = {0, "15MachineListView"};
    uintptr_t *vt = storage + 1;
    uint8_t pixels[64][128], untouched[64][128];
    storage[0] = (uintptr_t)typeinfo;
    vt[39] = (uintptr_t)view_knob;
    vt[41] = (uintptr_t)view_parameter;
    put_pointer(parameter_object, 16, sound_reference);
    put_pointer(sound_reference, 16, ui_sound);
    put_pointer(view, 0, vt);
    view[21] = 1;
    put_pointer(controller, 28, node);
    put_pointer(node, 0, controller + 20);
    put_pointer(node, 8, view);
    ui_sound[126] = 69;
    expected_view = view;
    assert(ob1_page.base.knob[K_WAV1].draw == draw_wave);
    assert(ob1_page.base.knob[K_WAV2].draw == draw_wave);
    assert(ob1_page.base.knob[K_WAV1].flags & CM_DRAW);
    assert(ob1_page.base.knob[K_WAV2].flags & CM_DRAW);
    for (int first = 0; first < 2; first++) for (int second = 0; second < 2; second++)
        for (int position = 0; position < 64; position++) {
            ui_values[K_TYP1] = first << 8;
            ui_values[K_TYP2] = second << 8;
            ui_values[K_WAV1] = position << 8;
            ui_values[K_WAV2] = (63 - position) << 8;
            memset(pixels, 0, sizeof(pixels));
            ui_reads = 0;
            ob1_draw(pixels, controller);
            assert(ui_reads == 4);
            waveform_pixels(pixels, 0, first * 64 + position);
            waveform_pixels(pixels, 1, second * 64 + 63 - position);
        }
    memset(pixels, 0xa5, sizeof(pixels));
    memcpy(untouched, pixels, sizeof(pixels));
    ui_page = 1;
    ob1_draw(pixels, controller);
    assert(!memcmp(pixels, untouched, sizeof(pixels)));
    ui_page = 0;
    ui_sound[126] = 3;
    ob1_draw(pixels, controller);
    assert(!memcmp(pixels, untouched, sizeof(pixels)));
    ui_sound[126] = 69;
    view[21] = 0;
    ob1_draw(pixels, controller);
    assert(!memcmp(pixels, untouched, sizeof(pixels)));
    view[21] = 1;
    modal_storage[0] = (uintptr_t)modal_typeinfo;
    put_pointer(modal, 0, modal_storage + 1);
    modal[21] = 1;
    put_pointer(node, 0, modal_node);
    put_pointer(modal_node, 8, modal);
    put_pointer(modal_node, 0, controller + 20);
    ob1_draw(pixels, controller);
    assert(!memcmp(pixels, untouched, sizeof(pixels)));
    /* A corrupt/cyclic view list must stop without painting. */
    put_pointer(node, 0, node);
    ob1_draw(pixels, controller);
    assert(!memcmp(pixels, untouched, sizeof(pixels)));
}

static void test_level_graphics(void)
{
    static const int controls[] = {K_FND1, K_OCT1, K_FND2, K_OCT2, K_NOIS};
    uint8_t pixels[64][128];
    char buf[16];
    static const char *const labels[] = {"OFF", "-3DB", "ON"};
    for (unsigned k = 0; k < sizeof(controls) / sizeof(controls[0]); k++) {
        const struct cm_knob *p = &ob1_page.extra[controls[k] - 8];
        assert(p->draw == draw_level && (p->flags & CM_DRAW));
        for (int value = 0; value <= 127; value++) {
            int state = (value >= 43) + (value >= 86);
            int top = 12 - 6 * state;
            memset(pixels, 0, sizeof pixels);
            p->draw(pixels, 20, 20, value << 8, 0);
            p->fmt(buf, value << 8, 0, 69);
            assert(!strcmp(buf, labels[state]));
            for (int r = 0; r < KNOB_PX; r++) {
                /* The handle extends beyond the slot only at its selected position. */
                assert(pixels[20 + 16 - r][25] == (r >= top && r < top + 5));
                assert(pixels[20 + 16 - r][29] ==
                       (r == 0 || r == 16 || (r >= top && r < top + 5 && r != top + 2)));
                assert(pixels[20 + 16 - r][37] == (r == 2 || r == 8 || r == 14));
            }
        }
    }
}

static void param(int k, int value)
{
    fw_set_param(value, 0, k < 8 ? 17 + k : 46 + k - 8);
}

static void reset(void)
{
    uint16_t sound[81];
    memset(sound, 0xa5, sizeof(sound));
    memset((void *)fw_voice_params, 0, sizeof(fw_voice_params));
    cleared_track = -1;
    ob1_switch(0, 3, (uint8_t *)sound);
    assert(cleared_track == 0);
    for (int i = 0; i < 81; i++)
        assert(sound[i] == (i == 10 + 22 ? 67 << 8 : 0xa5a5));
    for (int k = 0; k < K_COUNT; k++)
        assert(cm_src_value(0, k) == (k == K_FIN2 ? 67 << 8 : 0));
    assert(ob1_page.base.knob[K_FIN2].def == 64 << 8);
    for (int k = 0; k < K_COUNT; k++) {
        const struct cm_knob *p = k < 8 ? &ob1_page.base.knob[k] : &ob1_page.extra[k - 8];
        /* Translation checks use neutral defaults; firmware checks cover the fresh patch. */
        param(k, p->def);
    }
    memset((void *)ready, 0, sizeof(ready));
    memset((void *)fw_render_machine, 0, sizeof(fw_render_machine));
    fw_render_machine[0] = OB1_ID;
    fw_voice_note[0] = 60 << 16;
    fw_voice_start = 1;
}

static void parity(const struct ob1_knobs *knobs, int semitones1, int semitones2, int fine1, int fine2)
{
    struct ob1_voice reference;
    int32_t blocks[8 * 32], expected[32];
    memset(blocks, 0x5a, sizeof(blocks));
    memset((void *)ready, 0, sizeof(ready));
    ob1_voice_init(&reference, 1);
    ob1_voice_render(&reference, knobs,
                     ob1_pitch_inc((63 + semitones1) * 65536 + fine1, fw_pitch_tab),
                     ob1_pitch_inc((63 + semitones2) * 65536 + fine2, fw_pitch_tab), expected);
    ob1_render(blocks);
    assert(!memcmp(blocks, expected, sizeof(expected)));
    for (int i = 32; i < 8 * 32; i++)
        assert(blocks[i] == 0x5a5a5a5a);  /* Other machines retain their audio. */
}

static void test_silent_voice(void)
{
    int32_t blocks[8 * 32];
    reset();
    ob1_render(blocks);
    struct ob1_voice before = voice[0];
    fw_voice_start = 0;
    fw_amp_env[1] = 0;
    memset(blocks, 0x5a, sizeof blocks);
    ob1_render(blocks);
    assert(!memcmp(&before, &voice[0], sizeof before));
    for (int i = 0; i < 32; i++)
        assert(blocks[i] == 0);
    fw_voice_start = 1;
    ob1_render(blocks);
    assert(voice[0].vco1.vco.phase != before.vco1.vco.phase);
    assert(voice[0].vco2.vco.phase != before.vco2.vco.phase);
}

static void test_layout_and_ranges(void)
{
    static const uint8_t mixer_page[8] = {8, 9, 10, CM_SRC_BLANK, 11, 12, 13, 14};
    static const char *const names[K_COUNT] = {
        "FRQ1", "FIN1", "TYP1", "WAV1", "FRQ2", "FIN2", "TYP2", "WAV2",
        "FND1", "OCT1", "XMOD", "FND2", "OCT2", "SYNC", "NOIS"
    };
    char buf[16];
    assert(ob1_machine.id == 69 && ob1_page.base.abi == cm_ui_v32);
    assert(ob1_page.page_count == 2);
    assert(!memcmp(ob1_pages[1].parameter, mixer_page, sizeof(mixer_page)));
    for (int k = 0; k < K_COUNT; k++) {
        const struct cm_knob *p = k < 8 ? &ob1_page.base.knob[k] : &ob1_page.extra[k - 8];
        assert(!strcmp(p->name, names[k]));
        assert((p->flags & (CM_RANGE | CM_DEFAULT)) == (CM_RANGE | CM_DEFAULT));
        if (k < 8)
            assert(ob1_pages[0].parameter[k] == k);
        else
            assert(p->min == 0 && p->max == 0x7f00);
    }
    assert(ob1_page.base.knob[K_TYP1].max == 0x100);
    assert(ob1_page.base.knob[K_TYP2].max == 0x100);
    assert(family(-1) == 0 && family(0) == 0 && family(0x7f) == 0);
    assert(family(0x80) == 1 && family(0x100) == 1 && family(0x7f00) == 1);
    for (int raw = 0; raw <= 0x7f00; raw++) {
        int v = raw >> 8;
        assert(zone(raw, 2) == (v >= 64));
        assert(zone(raw, 3) == (v >= 43) + (v >= 86));
        assert(fine(raw) >= -32768 && fine(raw) <= 32768);
        if (raw)
            assert(fine(raw) >= fine(raw - 1));
        if (raw != 0x4000)
            assert(fine(raw) != 0);
    }
    assert(fine(0) == -32768 && fine(0x4000) == 0 && fine(0x7f00) == 32768);
    assert(fine(-1) == -32768 && fine(0xffff) == 32768);
    assert(knob(-1, 63) == 0 && knob(0x7f00, 63) == 63);
    for (int position = 0; position < 64; position++) {
        assert(wave(0, position << 8) == position);
        assert(wave(0x100, position << 8) == 64 + position);
    }
    frq_text(buf, 0, 0, 69); assert(!strcmp(buf, "-32"));
    frq_text(buf, 32 << 8, 0, 69); assert(!strcmp(buf, "+0"));
    frq_text(buf, 63 << 8, 0, 69); assert(!strcmp(buf, "+31"));
    fine_text(buf, 0, 0, 69); assert(!strcmp(buf, "-50.0"));
    fine_text(buf, 64 << 8, 0, 69); assert(!strcmp(buf, "+0.0"));
    fine_text(buf, 67 << 8, 0, 69); assert(!strcmp(buf, "+2.4"));
    fine_text(buf, 127 << 8, 0, 69); assert(!strcmp(buf, "+50.0"));
    fine_text(buf, 65 << 8, 0, 69); assert(!strcmp(buf, "+0.8"));
    type_text(buf, 0x100, 0, 69); assert(!strcmp(buf, "PULS"));
    level_text(buf, 64 << 8, 0, 69); assert(!strcmp(buf, "-3DB"));
    switch_text(buf, 64 << 8, 0, 69); assert(!strcmp(buf, "ON"));
}

static void test_pitch_and_wave_translation(void)
{
    reset();
    struct ob1_knobs knobs = {63, 63, 6, 3, 0, 0};
    parity(&knobs, 0, 0, 0, 0);
    param(K_FRQ1, 44 << 8); parity(&knobs, 12, 0, 0, 0);
    param(K_FRQ1, 32 << 8); param(K_FRQ2, 44 << 8); parity(&knobs, 0, 12, 0, 0);
    reset();
    param(K_FIN1, 65 << 8); parity(&knobs, 0, 0, 520, 0);
    param(K_FIN1, 64 << 8); param(K_FIN2, 63 << 8); parity(&knobs, 0, 0, 0, -512);
    param(K_FIN2, (64 << 8) + 200); parity(&knobs, 0, 0, 0, 406);
    reset();
    param(K_TYP1, 0x100); knobs.wav1 = 127; parity(&knobs, 0, 0, 0, 0);
    param(K_WAV1, 0); knobs.wav1 = 64; parity(&knobs, 0, 0, 0, 0);
    param(K_TYP2, 0x100); param(K_WAV2, 0); knobs.wav2 = 64; parity(&knobs, 0, 0, 0, 0);
}

static void test_mixer_translation(void)
{
    struct ob1_knobs knobs = {64, 64, 0, 0, 0, 0};
    reset();
    param(K_TYP1, 0x100); param(K_WAV1, 0);
    param(K_TYP2, 0x100); param(K_WAV2, 0);
    for (int mix1 = 0; mix1 < 9; mix1++) for (int mix2 = 0; mix2 < 9; mix2++)
        for (int mod = 0; mod < 4; mod++) {
            param(K_FND1, ZONE_AT(mix1 / 3, 3) << 8);
            param(K_OCT1, ZONE_AT(mix1 % 3, 3) << 8);
            param(K_FND2, ZONE_AT(mix2 / 3, 3) << 8);
            param(K_OCT2, ZONE_AT(mix2 % 3, 3) << 8);
            param(K_XMOD, mod & DSP_XMOD ? 0x7f00 : 0);
            param(K_SYNC, mod & DSP_SYNC ? 0x7f00 : 0);
            param(K_NOIS, ZONE_AT(mod % 3, 3) << 8);
            knobs.mix1 = mix1; knobs.mix2 = mix2; knobs.mod = mod; knobs.nois = mod % 3;
            parity(&knobs, 0, 0, 0, 0);
        }
}

static void test_midi_voice_reset(void)
{
    int32_t blocks[8 * 32], expected[8][32];
    struct ob1_voice other;
    reset();
    param(K_NOIS, 127 << 8);
    param(K_OCT1, 127 << 8);
    param(K_SYNC, 127 << 8);
    param(K_XMOD, 127 << 8);
    for (int b = 0; b < 8; b++) {
        ob1_render(blocks);
        memcpy(expected[b], blocks, sizeof expected[b]);
    }
    ob1_voice_init(&voice[1], 2);
    ready[1] = 1;
    other = voice[1];
    struct ob1_voice before = voice[0];
    for (int track = -1; track <= 16; track++)
        if (track != 0 && track != 1)
            assert(ob1_midi_cc_parameter(track, 69, 9) == 0);
    assert(ob1_midi_cc_parameter(0, 3, 9) == 0);
    assert(ready[0] && ready[1]);
    assert(ob1_midi_cc_parameter(0, 69, 9) == 0);
    assert(!ready[0] && ready[1]);
    assert(!memcmp(&before, &voice[0], sizeof before));
    fw_voice_start = 0;
    fw_amp_env[1] = 0;
    ob1_render(blocks);
    assert(!ready[0]);
    assert(!memcmp(&before, &voice[0], sizeof before));
    fw_voice_start = 1;
    for (int repeat = 0; repeat < 3; repeat++) {
        assert(ob1_midi_cc_parameter(0, 69, 9) == 0);
        for (int b = 0; b < 8; b++) {
            ob1_render(blocks);
            assert(ready[0]);
            assert(!memcmp(expected[b], blocks, sizeof expected[b]));
        }
    }
    assert(ready[1] && !memcmp(&other, &voice[1], sizeof other));
}

static void test_midi_lookup(void)
{
    assert(ob1_midi_cc_parameter(0, 69, 24) == 164);
    assert(ob1_midi_cc_parameter(7, 69, 30) == 170);
    for (int track = 8; track <= 16; track++)
        assert(ob1_midi_cc_parameter(track, 69, 24) == 0);
    assert(ob1_midi_cc_parameter(-1, 69, 24) == 0);
    assert(ob1_midi_cc_parameter(0, 3, 24) == 0);
    assert(ob1_midi_cc_parameter(0, 69, 23) == 0);
    assert(ob1_midi_cc_parameter(0, 69, 31) == 0);
}

int main(void)
{
    test_layout_and_ranges();
    test_pitch_and_wave_translation();
    test_mixer_translation();
    test_midi_lookup();
    test_midi_voice_reset();
    test_waveform_graphics();
    test_level_graphics();
    test_silent_voice();
    puts("Digitakt controls: layout, defaults, ranges, independent pitch and 324 mixer/routing audio cases passed");
    return 0;
}
