/* SPDX-License-Identifier: GPL-2.0-or-later */
/* ob1: the Oberheim OB-1's oscillator section as an SRC machine on core 3.0: two VCOs (machine/dsp.c), VCO1 at
 * the trig's note and TUNE, VCO2 FRQ2 semitones from it, each with its wave type and WAVE and its FUND and
 * OCT mix, VCO2 synced to VCO1 and VCO1 cross-modulated by VCO2 on MOD, and noise; the track's filter,
 * envelope, level, LFOs and sends work on it as on a sample.
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
#define K_FRQ2      1                   /* PLAY's slot, 0-63: VCO2, -32 to +31 semitones from VCO1 */
#define K_WAV1      2                   /* BR's: its range, 0-127, and its knob as they are */
#define K_WAV2      3                   /* SAMP's slot, 0-127, not opening the sample list */
#define K_MOD       4                   /* SLICE's slot, 0-3: OFF, SYNC, XMOD, BOTH */
#define K_MIX1      5                   /* LEN's slot, 0-8: the nine settings of VCO1 FUND x OCT */
#define K_MIX2      6                   /* GRID's slot, 0-8: VCO2's */
#define K_NOIS      7                   /* LEV's slot, 0-2: NOISE off, low, high */
#define ID_BR       134                 /* BR's parameter id (SLICE's knob C): a plain knob, for a slot whose own turns oddly */

/* ---- the sound (ev_render_voices: the render, interrupt level) ----------------------------------- */

static struct dsp_osc vco1[8], vco2[8];    /* free-running, as the OB-1's VCOs: never reset */
static struct dsp_hpf hpf[8];
static uint32_t noise[8] = {1, 2, 3, 4, 5, 6, 7, 8};   /* each voice its own: never 0 */

/* A mixer setting, 0-2 (off, low, high), as its gain. */
static const int32_t mix_gain[3] = {0, DSP_MIX_LOW, DSP_MIX_HIGH};
static const int32_t noise_gain[3] = {0, DSP_MIX_NOISE_LOW, DSP_MIX_NOISE_HIGH};
static const int32_t zero[32];                 /* a source not rendered */

/* WAV1 and WAV2 0-63: saw type, triangle (0) to saw (63), as g -1 to +1, Q15. */
static int32_t wave_g(int32_t w)
{
    return -32768 + (w << 16) / 63;
}

/* WAV1 and WAV2 64-127: pulse type, from the narrowest pulse (64) to the square (127), as the OB-1's WAVE pot
 * turned clockwise ends on the square (trimmed to 50 %). shortcut: the narrowest width is a guess, 10 %;
 * set it from a real OB-1's sweep (#15). */
#define PW_MIN  429496730u                         /* 10 % of 2^32 */
static uint32_t wave_width(int32_t w)
{
    return PW_MIN + (uint32_t)(w - 64) * ((0x80000000u - PW_MIN) / 63);
}

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

/* WAV1/WAV2's value as a waveshape. */
static void shape(struct dsp_shape *sh, int32_t w)
{
    if (w < 64)
        dsp_shape_saw(sh, wave_g(w));
    else
        dsp_shape_pulse(sh, wave_width(w));
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
        int32_t pitch, mix1, mix2, mod, i, f1[32], s1[32], f2[32], s2[32], gain[4];
        const int32_t *src[4];
        uint32_t inc1, inc2;
        struct dsp_shape sh1, sh2;
        if (fw_render_machine[t] != OB1_ID)
            continue;
        if (silent(t)) {
            for (i = 0; i < 32; i++)
                blocks[32 * t + i] = 0;
            continue;
        }
        /* the note and TUNE as the playback takes a sample's: the pitch table at 1 for note 60, TUNE 0 */
        pitch = ((int32_t)p[K_TUNE] - 0x4000) * 256 + fw_voice_note[t] + (3 << 16);
        inc1 = pitch_inc(pitch);
        inc2 = pitch_inc(pitch + ((knob(p, K_FRQ2, 63) - 32) << 16));
        shape(&sh1, knob(p, K_WAV1, 127));
        shape(&sh2, knob(p, K_WAV2, 127));
        mix1 = knob(p, K_MIX1, 8);
        mix2 = knob(p, K_MIX2, 8);
        mod = knob(p, K_MOD, 3);                                       /* DSP_SYNC | DSP_XMOD */
        if (!(mod & DSP_XMOD) || !mix1)
            mod &= ~DSP_XMOD;                                          /* moves only VCO1 */
        if (!mix2)
            mod &= ~DSP_SYNC;                                          /* moves only VCO2 */
        if (mod)
            dsp_pair(&vco1[t], &vco2[t], inc1, inc2, &sh1, &sh2, mod, f1, s1, f2, s2, 32);
        else {                                                         /* each alone, or not at all */
            if (mix1)
                dsp_osc_block(&vco1[t], inc1, &sh1, f1, s1, 32);
            if (mix2)
                dsp_osc_block(&vco2[t], inc2, &sh2, f2, s2, 32);
        }
        src[0] = mix1 ? f1 : zero;                                     /* FUND and OCT, each VCO */
        src[1] = mix1 ? s1 : zero;
        src[2] = mix2 ? f2 : zero;
        src[3] = mix2 ? s2 : zero;
        gain[0] = mix_gain[mix1 / 3];
        gain[1] = mix_gain[mix1 % 3];
        gain[2] = mix_gain[mix2 / 3];
        gain[3] = mix_gain[mix2 % 3];
        dsp_voice_out(&hpf[t], src, gain, &noise[t], noise_gain[knob(p, K_NOIS, 2)], &blocks[32 * t], 32);
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
    static const char *const word[3] = {"OFF", "LO", "HI"};
    int32_t v = value >> 8, n = 0;
    const char *w;
    (void)ctx;
    (void)machine;
    for (w = word[v < 0 ? 0 : v > 2 ? 2 : v]; *w; )
        buf[n++] = *w++;
    buf[n] = 0;
    return 1;
}

/* MOD's text. */
static int32_t mod_text(char *buf, int32_t value, int32_t ctx, int32_t machine)
{
    static const char *const word[4] = {"OFF", "SYNC", "XMOD", "BOTH"};
    int32_t v = value >> 8, n = 0;
    const char *w;
    (void)ctx;
    (void)machine;
    for (w = word[v < 0 ? 0 : v > 3 ? 3 : v]; *w; )
        buf[n++] = *w++;
    buf[n] = 0;
    return 1;
}

/* MIX1's and MIX2's text: FUND and OCT as - (off), L (low) or H (high), "H/-"; the pop-up spells it out. */
static int32_t mix_text(char *buf, int32_t value, int32_t ctx, int32_t machine)
{
    static const char lvl[3] = {'-', 'L', 'H'};
    static const char *const word[3] = {"OFF", "LO", "HI"};
    int32_t v = value >> 8, n = 0;
    const char *w;
    (void)machine;
    v = v < 0 ? 0 : v > 8 ? 8 : v;
    if (!ctx) {
        buf[0] = lvl[v / 3];
        buf[1] = '/';
        buf[2] = lvl[v % 3];
        buf[3] = 0;
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

/* A knob's value set in the sound, which is what is saved and shown, and in the engine (fw_set_param alone
 * leaves the sound as it was). */
static void set(uint8_t *sound, int32_t track, int k, int32_t v)
{
    *(uint16_t *)(sound + FW_SOUND_PARAM(k)) = (uint16_t)(v << 8);
    fw_set_param(v << 8, track, 17 + k);
}

/* A track turns to OB-1: both VCOs saws in unison, VCO1's FUND high, VCO2 and noise off, no sync or
 * cross mod. CM_DEFAULT alone does not
 * reach the slots on a switch (they keep SLICE's stock defaults: MIX1 0, all off, silent). */
static void ob1_switch(int32_t track, int32_t from, uint8_t *sound)
{
    (void)from;
    set(sound, track, K_FRQ2, 32);
    set(sound, track, K_WAV1, 63);
    set(sound, track, K_WAV2, 63);
    set(sound, track, K_MIX1, 6);
    set(sound, track, K_MIX2, 0);
    set(sound, track, K_MOD, 0);
    set(sound, track, K_NOIS, 0);
}

static const struct cm_ui ob1_page = {
    .abi = cm_ui_v3,
    .page_from = 3,                                     /* SLICE's page, as every added machine gets */
    .on_switch = ob1_switch,
    .knob = {
        /* the slots whose own UI records turn oddly (PLAY, SAMP, LEN, GRID) borrow BR's */
        [K_FRQ2] = { .name = "FRQ2", .lname = "VCO2 freq", .look = ID_BR, .flags = CM_RANGE | CM_DEFAULT,
                     .min = 0, .max = 63 << 8, .def = 32 << 8, .fmt = frq_text },
        [K_WAV1] = { .name = "WAV1", .lname = "VCO1 wave", .fmt = wave_text },
        [K_WAV2] = { .name = "WAV2", .lname = "VCO2 wave", .look = ID_BR, .flags = CM_NOT_SAMPLE | CM_RANGE |
                     CM_DEFAULT, .min = 0, .max = 127 << 8, .def = 63 << 8, .fmt = wave_text },
        [K_MOD] = { .name = "MOD", .lname = "Sync/cross mod", .look = ID_BR, .flags = CM_RANGE | CM_DEFAULT,
                    .min = 0, .max = 3 << 8, .def = 0, .fmt = mod_text },
        [K_MIX1] = { .name = "MIX1", .lname = "VCO1 mix", .look = ID_BR, .flags = CM_RANGE | CM_DEFAULT,
                     .min = 0, .max = 8 << 8, .def = 6 << 8, .fmt = mix_text },   /* FUND high, OCT off */
        [K_MIX2] = { .name = "MIX2", .lname = "VCO2 mix", .look = ID_BR, .flags = CM_RANGE | CM_DEFAULT,
                     .min = 0, .max = 8 << 8, .def = 0, .fmt = mix_text },        /* off */
        [K_NOIS] = { .name = "NOIS", .lname = "Noise", .look = ID_BR, .flags = CM_RANGE | CM_DEFAULT,
                     .min = 0, .max = 2 << 8, .def = 0, .fmt = noise_text },
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
