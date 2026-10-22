/* SPDX-License-Identifier: GPL-2.0-or-later */
/* OB-1 Digitone adapter: the shared Oberheim OB-1 oscillator section
 * (dsp/dsp.c, dsp/voice.c, shared with the Digitakt machine) as a Digitone
 * mk1 / Keys machine (id 69 "OB-1"), on the machines-dn1 companion
 * (digitone-mk1/machines.h). Thin: it only decodes its sounds' SYN knobs
 * into the engine's compact controls, supplies phase increments and the
 * output block, and leaves the filter, AMP envelope, mixer and effects to
 * the Digitone.
 *
 * Control map: the Digitakt machine's fifteen controls over two pages, on
 * borrowed stock SYN ranges, so locks, LFOs, CCs and presets work untouched
 * (no custom params, no sites). See param.h for the index/range tables.
 *   SYN1 A-H (MIX page): FND1 OCT1 XMOD  --  FND2 OCT2 SYNC NOIS
 *   SYN2 A-H (OSC page): FRQ1 FIN1 TYP1 WAV1 FRQ2 FIN2 TYP2 WAV2
 * The decode is the Digitakt machine's: independent coarse+fine pitch for
 * each VCO, a family and a shape per VCO, FUND/OCT as two 3-zone levels,
 * XMOD and SYNC as two separate switches, and noise in three zones.
 *
 * Pitch: dnm_phase_inc over the voice pitch word plus FRQ (semitones) and
 * FIN (Q16 semitones) is each VCO's step in the DSP's own units (Q32 steps
 * at 48 kHz), so the Digitone plays the same pitches as the Digitakt
 * machine. Out: the shared voice renders Q31 and is written unscaled,
 * exactly as the Digitakt machine writes it, so the two devices' blocks
 * match for the same knobs; a single oscillator at ON peaks about at
 * DNM_PEAK, as loud as FM at the stock defaults. Flags stay 0: the
 * companion owns the AMP envelope and velocity. Zero sounding voices cost
 * ~zero (the companion skips silent voices; the render writes zeros for
 * fused ones).
 *
 * Polyphony: newest-wins per track (OB1_TRACK_VOICES each, so tracks
 * never choke each other); releases free immediately; ob1_reap() on the
 * UI tick clears unobserved ends (a render-side clock freezes with zero
 * callers). The cap is pending fwcheck/hardware measurement: see README.
 */
#include "digitone-mk1/machines.h"
#include "../../dsp/voice.h"
#include "param.h"

#define OB1_ID  69

/* Sum an 8.8 value and up to two LFO modulations, clamped to the slot's
 * stock range, as the stock does. */
static int32_t ob1_add_lfo(int32_t slot, int32_t val, int32_t mn, int32_t mx,
                            int32_t d0, int32_t m0, int32_t d1, int32_t m1)
{
    if (d0 == slot) {
        val += m0;
        if (val < mn)
            val = mn;
        else if (val > mx)
            val = mx;
    }
    if (d1 == slot) {
        val += m1;
        if (val < mn)
            val = mn;
        else if (val > mx)
            val = mx;
    }
    return val;
}

/* Voice cap: newest-wins per track. Bookkeeping: birth order from a
 * render-side clock; releases free immediately; ob1_reap() on the UI tick
 * clears ends the render stopped observing. */
#define OB1_TRACK_VOICES  2
#define OB1_TICK_REAP     120u
uint8_t ob1_sounding[8];
uint32_t ob1_birth[8];
uint32_t ob1_seen[8];
static int32_t ob1_track[8];
static uint32_t ob1_clock;
uint32_t ob1_ticks;

void ob1_reap(void)
{
    int32_t w;
    for (w = 0; w < 8; w++) {
        if (ob1_sounding[w] &&
            (int32_t)(ob1_ticks - ob1_seen[w]) > (int32_t)OB1_TICK_REAP) {
            ob1_sounding[w] = 0;
            ob1_birth[w] = 0;
        }
    }
}

static struct ob1_voice ob1_voices[8];
static uint8_t ob1_ready[8];

static char *put_int(char *buf, int32_t v)
{
    char t[8];
    int32_t n = 0;
    if (v < 0) {
        *buf++ = '-';
        v = -v;
    }
    do {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v && n < 7);
    while (n)
        *buf++ = t[--n];
    return buf;
}

/* Value texts (the 8.8 value in, short text out). The overlay calls these
 * through the knob table; the stock turn popups keep FM long names. Each
 * level/switch text is per knob, so it can zone within that knob's stock
 * range. */
static const char *level_word(int32_t i, int32_t value)
{
    static const char *const word[3] = {"OFF", "-3DB", "ON"};
    return word[ob1_zone(i, value >> 8, 3)];
}

static const char *switch_word(int32_t i, int32_t value)
{
    return ob1_zone(i, value >> 8, 2) ? "ON" : "OFF";
}

static void copy_text(char *buf, const char *s)
{
    while (*s)
        *buf++ = *s++;
    *buf = 0;
}

/* Machine label -> value text, one function a control (machines.h's fmt). */
#define LEVEL_TEXT(fn, idx) \
    static void fn(int32_t value, char *buf) { copy_text(buf, level_word(idx, value)); }
#define SWITCH_TEXT(fn, idx) \
    static void fn(int32_t value, char *buf) { copy_text(buf, switch_word(idx, value)); }
LEVEL_TEXT(ob1_fnd1_text, K_FND1)
LEVEL_TEXT(ob1_oct1_text, K_OCT1)
LEVEL_TEXT(ob1_fnd2_text, K_FND2)
LEVEL_TEXT(ob1_oct2_text, K_OCT2)
LEVEL_TEXT(ob1_nois_text, K_NOIS)
SWITCH_TEXT(ob1_xmod_text, K_XMOD)
SWITCH_TEXT(ob1_sync_text, K_SYNC)

/* FRQ1/FRQ2: signed semitones from the note. */
static void ob1_frq_text(int32_t value, char *buf)
{
    int32_t s = ob1_frq_of(value >> 8);
    if (s >= 0)
        *buf++ = '+';
    *put_int(buf, s) = 0;
}

/* FIN1/FIN2: signed cents with a tenth, as the Digitakt machine. */
static void ob1_fine_text(int32_t value, char *buf)
{
    int32_t q = ob1_fine_of(value >> 8);
    int32_t c = (q * 1000 + (q < 0 ? -32768 : 32768)) / 65536;   /* tenths */
    if (c < 0) {
        *buf++ = '-';
        c = -c;
    } else {
        *buf++ = '+';
    }
    buf = put_int(buf, c / 10);
    *buf++ = '.';
    *buf++ = (char)('0' + c % 10);
    *buf = 0;
}

/* TYP1/TYP2. */
static void ob1_type_text(int32_t value, char *buf)
{
    copy_text(buf, ob1_family_of(value >> 8) ? "PULS" : "SAW");
}

/* WAV1/WAV2: the shape position, 0-63 (the family is on the TYP knob). */
static void ob1_wave_text(int32_t value, char *buf)
{
    int32_t p = value >> 8;
    p = p < 0 ? 0 : p > 127 ? 127 : p;
    *put_int(buf, p >> 1) = 0;
}

const struct dnm_knob ob1_knobs[DNM_KNOBS] = {
    [K_FND1] = {"FND1", ob1_fnd1_text, 0},
    [K_OCT1] = {"OCT1", ob1_oct1_text, 0},
    [K_XMOD] = {"XMOD", ob1_xmod_text, 0},
    [K_FND2] = {"FND2", ob1_fnd2_text, 0},
    [K_OCT2] = {"OCT2", ob1_oct2_text, 0},
    [K_SYNC] = {"SYNC", ob1_sync_text, 0},
    [K_NOIS] = {"NOIS", ob1_nois_text, 0},
    [K_FRQ1] = {"FRQ1", ob1_frq_text, 0},
    [K_FIN1] = {"FIN1", ob1_fine_text, 0},
    [K_TYP1] = {"TYP1", ob1_type_text, 0},
    [K_WAV1] = {"WAV1", ob1_wave_text, 0},
    [K_FRQ2] = {"FRQ2", ob1_frq_text, 0},
    [K_FIN2] = {"FIN2", ob1_fine_text, 0},
    [K_TYP2] = {"TYP2", ob1_type_text, 0},
    [K_WAV2] = {"WAV2", ob1_wave_text, 0},
};

/* A knob's 8.8 value with its LFO modulation folded in, as raw. */
static int32_t ob1_raw(int32_t voice, int32_t i, int32_t d0, int32_t m0,
                       int32_t d1, int32_t m1)
{
    return ob1_add_lfo(ob1_slot[i], (int32_t)FW_VOICE_PARAM(voice, ob1_slot[i]),
                       ob1_rmin[i] << 8, ob1_rmax[i] << 8, d0, m0, d1, m1) >> 8;
}

static void ob1_render(int32_t voice, int32_t *block, const struct dnm_voice *v)
{
    int32_t lfo0_dest, lfo0_mod, lfo1_dest, lfo1_mod;
    int32_t raw[OB1_KNOBS];
    int32_t i, w, rank;
    uint32_t inc1, inc2;
    struct ob1_knobs k;
    voice &= 7;

    ob1_seen[voice] = ob1_ticks;
    if (v->fresh) {
        ob1_sounding[voice] = 1;
        ob1_birth[voice] = ++ob1_clock;
        ob1_track[voice] = v->track;
    } else if (!v->gate) {
        ob1_sounding[voice] = 0;
    } else if (!ob1_sounding[voice]) {
        ob1_sounding[voice] = 1;
        ob1_birth[voice] = ++ob1_clock;
        ob1_track[voice] = v->track;
    }
    /* Per-track newest-wins: only same-track voices outrank. Stale
     * entries (awaiting reap) hold older births, so they never silence
     * a newer voice. */
    rank = 0;
    for (w = 0; w < 8; w++) {
        if (ob1_sounding[w] && ob1_track[w] == v->track &&
            (int32_t)(ob1_birth[w] - ob1_birth[voice]) > 0)
            rank++;
    }
    if (rank >= OB1_TRACK_VOICES) {
        for (i = 0; i < 32; i++)
            block[i] = 0;
        return;
    }

    /* The VCOs run freely between notes, as the OB-1's do: voices are
     * set up once and never reset, so each note starts at a different
     * phase. Seed (not 0) sets each voice's noise apart. */
    if (!ob1_ready[voice]) {
        ob1_voice_init(&ob1_voices[voice], (uint32_t)voice + 1u);
        ob1_ready[voice] = 1;
    }

    /* Knob values with the LFO modulation folded in, clamped to each
     * slot's own stock range. */
    lfo0_dest = (int32_t)FW_LFO(voice, 0)[FW_LFO_DEST];
    lfo0_mod = (int32_t)FW_LFO(voice, 0)[FW_LFO_MOD];
    lfo1_dest = (int32_t)FW_LFO(voice, 1)[FW_LFO_DEST];
    lfo1_mod = (int32_t)FW_LFO(voice, 1)[FW_LFO_MOD];
    for (i = 0; i < OB1_KNOBS; i++)
        raw[i] = ob1_raw(voice, i, lfo0_dest, lfo0_mod, lfo1_dest, lfo1_mod);

    k.wav1 = ob1_wave_of(raw[K_TYP1], raw[K_WAV1]);
    k.wav2 = ob1_wave_of(raw[K_TYP2], raw[K_WAV2]);
    k.mix1 = ob1_zone(K_FND1, raw[K_FND1], 3) * 3 + ob1_zone(K_OCT1, raw[K_OCT1], 3);
    k.mix2 = ob1_zone(K_FND2, raw[K_FND2], 3) * 3 + ob1_zone(K_OCT2, raw[K_OCT2], 3);
    k.mod  = ob1_zone(K_SYNC, raw[K_SYNC], 2) * DSP_SYNC |
             ob1_zone(K_XMOD, raw[K_XMOD], 2) * DSP_XMOD;
    k.nois = ob1_zone(K_NOIS, raw[K_NOIS], 3);

    inc1 = dnm_phase_inc(v->pitch + (ob1_frq_of(raw[K_FRQ1]) << 16) +
                         ob1_fine_of(raw[K_FIN1]));
    inc2 = dnm_phase_inc(v->pitch + (ob1_frq_of(raw[K_FRQ2]) << 16) +
                         ob1_fine_of(raw[K_FIN2]));
    ob1_voice_render(&ob1_voices[voice], &k, inc1, inc2, block);
    /* The shared voice writes Q1.31 already at the intended level; the
     * Digitakt machine writes it unscaled too, so the two devices match. */
}

/* MACHINES tile icon: 16 rows of 16 pixels, bit 15 the left. The Digitakt
 * machine menu's sawtooth (a ramp rising left to right, its reset edge, then
 * the next ramp), drawn for this tile. */
static const uint16_t ob1_icon[16] = {
    0x0000, 0x0000, 0x0000, 0x0000, 0x0060, 0x00A0, 0x0120, 0x0220,
    0x0422, 0x0824, 0x1028, 0x2030, 0x4020, 0x0000, 0x0000, 0x0000,
};

/* The knobs' switch defaults are in param.h: the overlay's FUNC+turn reset
 * writes the same table. */
const struct dnm_machine ob1_machine = {
    OB1_ID, "OB-1", ob1_icon, ob1_knobs, ob1_render, 0, ob1_defaults,
};
