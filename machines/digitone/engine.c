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
 * companion owns the AMP envelope and velocity. Silent voices cost ~zero
 * (the companion skips them; the render writes zeros for the rest).
 *
 * Voice policy: one note per track, as the OB-1 and the Digitakt machine.
 * The newest held note owns the track; releasing it returns to the newest
 * note still held. Each track keeps the same two VCOs across notes: the
 * oscillator state is the track's, not the Digitone voice's, so a note on
 * another voice only changes its pitch. A short crossfade on handover and
 * on a return keeps the change click-free. The note-on and block events
 * run before the companion's render; ob1_reap() on the UI tick clears
 * unobserved ends (a render-side clock freezes with zero callers).
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

/* One voice a track: the newest held note owns it; releasing that note
 * returns to the newest note still held. The state below is by track, so
 * a note on a different Digitone voice continues the same two VCOs. */
#define OB1_TRACKS       4
#define OB1_FADE         65         /* samples: the handover crossfade */
#define OB1_TICK_REAP    120u

uint32_t ob1_ticks;
static uint32_t ob1_block_no;
static uint8_t ob1_stack[OB1_TRACKS][8];    /* held voices, oldest first */
static uint8_t ob1_depth[OB1_TRACKS];
static uint8_t ob1_held[8];
static uint8_t ob1_voice_track[8];          /* track + 1; 0 idle */
static int8_t ob1_current[OB1_TRACKS] = {-1, -1, -1, -1};
static uint8_t ob1_fade_in[8], ob1_fade_out[8];
static uint8_t ob1_sounded[8];              /* it was the sounding voice last block */
static uint32_t ob1_seen[8];                /* block number last rendered */
static uint32_t ob1_track_tick[OB1_TRACKS];
static struct ob1_voice ob1_osc[OB1_TRACKS]; /* the same two VCOs across notes */
static struct ob1_voice ob1_tail[8];         /* a handover's fading-out state */
static uint8_t ob1_osc_ready[OB1_TRACKS];

void ob1_reap(void)
{
    int32_t t, v;
    for (t = 0; t < OB1_TRACKS; t++) {
        if (ob1_current[t] >= 0 &&
            (int32_t)(ob1_ticks - ob1_track_tick[t]) > (int32_t)OB1_TICK_REAP) {
            ob1_current[t] = -1;
            ob1_depth[t] = 0;
            for (v = 0; v < 8; v++) {
                if (ob1_voice_track[v] == t + 1) {
                    ob1_held[v] = 0;
                    ob1_voice_track[v] = 0;
                    ob1_sounded[v] = 0;
                    ob1_fade_in[v] = ob1_fade_out[v] = 0;
                }
            }
        }
    }
}

/* Field by field: a struct copy would call memcpy, which a mod does not have. */
static void ob1_osc_copy(struct dsp_osc *d, const struct dsp_osc *s)
{
    d->vco.phase = s->vco.phase;
    d->vco.inc = s->vco.inc;
    d->vco.rinc = s->vco.rinc;
    d->vco.shift = s->vco.shift;
    d->prev = s->prev;
    d->sub = s->sub;
    d->sub_prev = s->sub_prev;
}

static void ob1_voice_copy(struct ob1_voice *d, const struct ob1_voice *s)
{
    ob1_osc_copy(&d->vco1, &s->vco1);
    ob1_osc_copy(&d->vco2, &s->vco2);
    d->hpf.lp = s->hpf.lp;
    d->noise = s->noise;
    d->smooth[0] = s->smooth[0];
    d->smooth[1] = s->smooth[1];
}

static void ob1_stack_remove(int32_t t, int32_t voice)
{
    int32_t i;
    for (i = 0; i < ob1_depth[t]; i++) {
        if (ob1_stack[t][i] == voice) {
            for (; i + 1 < ob1_depth[t]; i++)
                ob1_stack[t][i] = ob1_stack[t][i + 1];
            ob1_depth[t]--;
            return;
        }
    }
}

static void ob1_stack_push(int32_t t, int32_t voice)
{
    ob1_stack_remove(t, voice);
    if (ob1_depth[t] < 8)
        ob1_stack[t][ob1_depth[t]++] = (uint8_t)voice;
}

static int32_t ob1_newest_held(int32_t t)
{
    return ob1_depth[t] ? ob1_stack[t][ob1_depth[t] - 1] : -1;
}

/* Make v the track's sounding voice. If the old voice was rendered in the
 * block just gone, keep a copy of the track's VCOs for it to fade out; the
 * new voice fades in from that same phase. */
static void ob1_switch(int32_t t, int32_t voice, int32_t tail)
{
    int32_t old = ob1_current[t];
    if (old == voice)
        return;
    if (old >= 0 && tail) {
        ob1_voice_copy(&ob1_tail[old], &ob1_osc[t]);
        ob1_fade_in[old] = 0;
        ob1_fade_out[old] = OB1_FADE;
    }
    ob1_fade_out[voice] = 0;
    ob1_fade_in[voice] = OB1_FADE;  /* a stolen voice may hold a level */
    ob1_current[t] = (int8_t)voice;
}

static void ob1_release(int32_t voice)
{
    int32_t t, next;
    if (!ob1_voice_track[voice] || !ob1_held[voice])
        return;
    t = ob1_voice_track[voice] - 1;
    ob1_held[voice] = 0;
    ob1_stack_remove(t, voice);
    if (ob1_current[t] != voice)
        return;
    next = ob1_newest_held(t);
    if (next >= 0)
        ob1_switch(t, next, ob1_sounded[voice]);
    else if (!ob1_sounded[voice])
        ob1_current[t] = -1;        /* an FM voice, or one never rendered */
    /* With no older held note, a sounding voice keeps its AMP release. */
}

/* A voice reused on another track: it cannot fade there, so the old track
 * hands over to its newest held note or falls silent. */
static void ob1_detach(int32_t voice, int32_t old_track)
{
    int32_t next;
    ob1_held[voice] = 0;
    ob1_stack_remove(old_track, voice);
    ob1_voice_track[voice] = 0;
    ob1_sounded[voice] = 0;
    ob1_fade_in[voice] = ob1_fade_out[voice] = 0;
    if (ob1_current[old_track] != voice)
        return;
    next = ob1_newest_held(old_track);
    ob1_current[old_track] = -1;
    if (next >= 0) {
        ob1_switch(old_track, next, 0);
        ob1_fade_in[next] = OB1_FADE;
    }
}

/* ev_voice_on, before the companion starts the voice's AMP envelope. */
void ob1_voice_on(int32_t voice, int32_t track, void *event)
{
    int32_t old_track;
    (void)event;
    voice &= 7;
    if (track < 0 || track >= OB1_TRACKS)
        return;
    old_track = ob1_voice_track[voice] ? ob1_voice_track[voice] - 1 : -1;
    if (old_track >= 0 && old_track != track)
        ob1_detach(voice, old_track);
    ob1_voice_track[voice] = (uint8_t)(track + 1);
    if (!ob1_osc_ready[track]) {
        ob1_voice_init(&ob1_osc[track], (uint32_t)track + 1u);
        ob1_osc_ready[track] = 1;
    }
    if (ob1_current[track] == voice) {
        ob1_fade_in[voice] = ob1_fade_out[voice] = 0;
    } else {
        int32_t old = ob1_current[track];
        ob1_switch(track, voice, old >= 0 && ob1_sounded[old]);
    }
    ob1_held[voice] = 1;
    ob1_stack_push(track, voice);
    ob1_track_tick[track] = ob1_ticks;
}

/* ev_render_voices, before the companion's per-voice render: settle last
 * block's releases, then return to a held key when the sounding one is
 * released. */
void ob1_block(int32_t *voices)
{
    uint32_t released = fw_gate_off;
    int32_t t, v;
    (void)voices;
    ob1_block_no++;
    for (v = 0; v < 8; v++) {
        if (released & (1u << v))
            ob1_release(v);
    }
    for (t = 0; t < OB1_TRACKS; t++) {
        v = ob1_current[t];
        if (v < 0)
            continue;
        if (ob1_sounded[v] && ob1_seen[v] != ob1_block_no - 1) {
            int32_t next = ob1_newest_held(t);
            ob1_current[t] = -1;
            ob1_sounded[v] = 0;
            if (next >= 0) {
                ob1_switch(t, next, 0);
                ob1_fade_in[next] = OB1_FADE;
            } else {
                ob1_fade_out[v] = 0;
            }
        }
        if (ob1_current[t] >= 0)
            ob1_track_tick[t] = ob1_ticks;
    }
}

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

/* Knob decode and one block from one oscillator state. */
static void ob1_render_state(int32_t voice, int32_t *block, const struct dnm_voice *v,
                             struct ob1_voice *state)
{
    int32_t lfo0_dest, lfo0_mod, lfo1_dest, lfo1_mod;
    int32_t raw[OB1_KNOBS];
    int32_t i;
    uint32_t inc1, inc2;
    struct ob1_knobs k;

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
    ob1_voice_render(state, &k, inc1, inc2, block);
}

/* A linear Q15 ramp across a block, complementing the other end of a
 * crossfade. left counts down from OB1_FADE; up ramps in, else out. */
#define OB1_FADE_STEP 512
static void ob1_fade(int32_t *block, uint8_t *left, int32_t up)
{
    int32_t i, g;
    for (i = 0; i < 32; i++) {
        if (*left) {
            g = up ? (OB1_FADE - *left) * OB1_FADE_STEP
                   : (*left - 1) * OB1_FADE_STEP;
            if (g > 32767)
                g = 32767;
            (*left)--;
        } else if (up) {
            continue;               /* fade-in finished: unity */
        } else {
            g = 0;
        }
        if (g != 32767)
            block[i] = ((block[i] >> 16) * g) << 1;
    }
}

static void ob1_render(int32_t voice, int32_t *block, const struct dnm_voice *v)
{
    int32_t t = v->track;
    int32_t i;
    voice &= 7;

    if (t < 0 || t >= OB1_TRACKS) {
        for (i = 0; i < 32; i++)
            block[i] = 0;
        ob1_seen[voice] = ob1_block_no;
        return;
    }
    if (!ob1_osc_ready[t]) {
        ob1_voice_init(&ob1_osc[t], (uint32_t)t + 1u);
        ob1_osc_ready[t] = 1;
    }
    if (voice == ob1_current[t]) {
        ob1_render_state(voice, block, v, &ob1_osc[t]);
        if (ob1_fade_in[voice])
            ob1_fade(block, &ob1_fade_in[voice], 1);
        ob1_sounded[voice] = 1;
    } else if (ob1_fade_out[voice]) {
        ob1_render_state(voice, block, v, &ob1_tail[voice]);
        ob1_fade(block, &ob1_fade_out[voice], 0);
        ob1_sounded[voice] = 0;
    } else {
        for (i = 0; i < 32; i++)
            block[i] = 0;
        ob1_sounded[voice] = 0;
    }
    ob1_seen[voice] = ob1_block_no;
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
