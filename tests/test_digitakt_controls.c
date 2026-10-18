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
static int cleared_track, writes;

void fw_fillrect(void *bmp, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t colour)
{
    (void)bmp; (void)x0; (void)y0; (void)x1; (void)y1; (void)colour;
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
    writes++;
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
    writes = 0;
    cleared_track = -1;
    ob1_switch(0, 3, (uint8_t *)sound);
    assert(cleared_track == 0 && writes == K_COUNT);
    for (int k = 0; k < K_COUNT; k++) {
        const struct cm_knob *p = k < 8 ? &ob1_page.base.knob[k] : &ob1_page.extra[k - 8];
        int slot = k < 8 ? 17 + k : 46 + k - 8;
        assert(sound[10 + slot] == p->def);
        assert(cm_src_value(0, k) == p->def);
    }
    assert(sound[10] == 0xa5a5);  /* Reserved slot 0 is untouched. */
    memset(ready, 0, sizeof(ready));
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
    memset(ready, 0, sizeof(ready));
    ob1_voice_init(&reference, 1);
    ob1_voice_render(&reference, knobs,
                     ob1_pitch_inc((63 + semitones1) * 65536 + fine1, fw_pitch_tab),
                     ob1_pitch_inc((63 + semitones2) * 65536 + fine2, fw_pitch_tab), expected);
    ob1_render(blocks);
    assert(!memcmp(blocks, expected, sizeof(expected)));
    for (int i = 32; i < 8 * 32; i++)
        assert(blocks[i] == 0x5a5a5a5a);  /* Other machines retain their audio. */
}

int main(void)
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
        assert(wave(0x7f00, position << 8) == 64 + position);
    }
    frq_text(buf, 0, 0, 69); assert(!strcmp(buf, "-32"));
    frq_text(buf, 32 << 8, 0, 69); assert(!strcmp(buf, "+0"));
    frq_text(buf, 63 << 8, 0, 69); assert(!strcmp(buf, "+31"));
    fine_text(buf, 0, 0, 69); assert(!strcmp(buf, "-50.0"));
    fine_text(buf, 64 << 8, 0, 69); assert(!strcmp(buf, "+0.0"));
    fine_text(buf, 127 << 8, 0, 69); assert(!strcmp(buf, "+50.0"));
    fine_text(buf, 65 << 8, 0, 69); assert(!strcmp(buf, "+0.8"));
    type_text(buf, 0x7f00, 0, 69); assert(!strcmp(buf, "PULS"));
    level_text(buf, 64 << 8, 0, 69); assert(!strcmp(buf, "-3DB"));
    switch_text(buf, 64 << 8, 0, 69); assert(!strcmp(buf, "ON"));
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
    param(K_TYP1, 0x7f00); knobs.wav1 = 127; parity(&knobs, 0, 0, 0, 0);
    param(K_WAV1, 0); knobs.wav1 = 64; parity(&knobs, 0, 0, 0, 0);
    param(K_TYP2, 0x7f00); param(K_WAV2, 0); knobs.wav2 = 64; parity(&knobs, 0, 0, 0, 0);
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
    puts("Digitakt controls: layout, defaults, ranges, independent pitch and 324 mixer/routing audio cases passed");
    return 0;
}
