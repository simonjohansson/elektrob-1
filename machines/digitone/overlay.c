/* SPDX-License-Identifier: GPL-2.0-or-later */
/* OB-1 Digitone overlay: full SYN1/SYN2 redraws on an OB-1 machine track
 * (digitone-mk1/machines.h).
 *
 * Stock SYN pages draw FM's pictures (algorithm diagram, group headings,
 * envelope graphs) and format values as FM, which the machines companion
 * cannot replace per track. This paints the whole content area on ev_draw
 * after the firmware drew the frame, for the Digitakt machine's two pages:
 *   SYN1 (MIX): FND1 OCT1 XMOD -- FND2 OCT2 SYNC NOIS
 *   SYN2 (OSC): FRQ1 FIN1 TYP1 WAV1 FRQ2 FIN2 TYP2 WAV2
 * Four columns by two rows a page, each cell the knob's icon
 * (machines/digitakt/knobs.c: wave, interval, routing, fader and noise
 * graphics, drawn pixel for pixel; the Digitakt machine's level fader and
 * a centre-zero bar for the fine tune) over its centred label. The value
 * shows in place of the label for a moment after its encoder turns; the
 * stock turn popups carry it too. Title bar, left rail and status bar stay
 * stock.
 *
 * Gating uses firmware state, never pixels: paint only when the active
 * track's sound plays OB-1, the showing key is SYN1/SYN2, the brain shows
 * no screen over the pages, and the shown view is on its first page. A
 * held sequencer step does NOT fail the gate: in GRID/STEP record the
 * overlay stays up and shows that step's lock values (slot-indexed rows:
 * the step row's word for the knob's sound slot, -1 = none); otherwise
 * trigs play notes and the kit shows, as stock. Copy/paste/clear toasts
 * yield the frame ~90 ticks. ev_tick re-arms only on change. Runs on the
 * UI task.
 */
#include "digitone-mk1/machines.h"
#include "../../dsp/dsp.h"
#include "../digitakt/knobs.h"
#include "param.h"

#define OB1_ID  69

/* UI-tick counter, shared with engine.c's fuse (last-seen stamps and
 * ob1_reap's horizon). Defined in engine.c; render only reads it. */
extern uint32_t ob1_ticks;
void ob1_reap(void);

extern const struct dnm_knob ob1_knobs[DNM_KNOBS];

#ifndef KEYVAR_AT
#error "KEYVAR_AT: the showing key's address (0x41368ce4 in 1.44)"
#endif

/* Brain/screen chain, as core_page_shown uses it: the key dispatcher's
 * brain keeps bottom/top screens at +0x54/+0x5c; bottom screen's page
 * manager at +8, shown view at +0xc0; a view's place at +144. */
#define BRAIN_BOTTOM    0x54
#define BRAIN_TOP       0x5c
#define SCREEN_MGR      8
#define MGR_VIEW        0xc0
#define VIEW_CUR        144

/* Content area and grid (panel coords, y = 0 top). Clear rect starts 3 px
 * left of the grid: stock SYN1 chrome animates under it while the rail
 * provably ends at x14 — clearing from 15 takes the bleed. */
#define CX0 18
#define CXC 15
#define CX1 127
#define CY0 12
#define CYT 11
#define CY1 63
#define COLS 4
#define ROWS 2

/* Each page's knobs, in the Digitakt machine's order. */
static const int8_t ob1_page1_knobs[8] = {
    K_FND1, K_OCT1, K_XMOD, K_BLANK, K_FND2, K_OCT2, K_SYNC, K_NOIS,
};
static const int8_t ob1_page2_knobs[8] = {
    K_FRQ1, K_FIN1, K_TYP1, K_WAV1, K_FRQ2, K_FIN2, K_TYP2, K_WAV2,
};

static uint32_t ob1_frames;
static uint32_t ob1_enc_at;
static int32_t ob1_enc = -1;
static int32_t ob1_enc_page = -1;
static void *ob1_brain;
static uint32_t ob1_trigs;
static uint32_t ob1_edit_rec;
static uint32_t ob1_live;
static uint32_t ob1_rec_down;
static uint32_t ob1_func_down;
static uint32_t ob1_ptn_down;
static uint32_t ob1_page_down;
static uint32_t ob1_msg_until;
#define OB1_MSG_HOLD 90u

static int32_t msg_suppressed(void)
{
    return (int32_t)(ob1_ticks - ob1_msg_until) < 0;
}

#define YB(yt) (63 - (yt))

/* One run of lit pixels per rectangle: the Digitakt port's draw, in the
 * Digitone's coords (bmp y = 0 bottom; yb_top the art's top row). */
static void draw_rows(void *bmp, int32_t x, int32_t yb_top,
                      const uint32_t *rows)
{
    int32_t r, c, c0;
    for (r = 0; r < KNOB_PX; r++)
        for (c = 0; c < KNOB_PX; ) {
            if (!(rows[r] >> (KNOB_PX - 1 - c) & 1)) {
                c++;
                continue;
            }
            for (c0 = c; c < KNOB_PX && rows[r] >> (KNOB_PX - 1 - c) & 1; c++)
                ;
            fw_fillrect(bmp, x + c0, yb_top - r, x + c - 1, yb_top - r, 1);
        }
}

/* A single level fader (FND/OCT/NOIS): ON above the centre -3 dB position,
 * OFF below it. The Digitakt machine's fader, its rows ported. */
static void draw_level(void *bmp, int32_t z, int32_t x, int32_t ytop)
{
    uint32_t rows[KNOB_PX];
    int32_t yb = YB(ytop), top = 12 - 6 * z, r;
    for (r = 0; r < KNOB_PX; r++)
        rows[r] = r == 0 || r == 16 ? 0x00fe0 : 0x00820;
    for (r = top; r < top + 5; r++)
        rows[r] |= 0x01ff0;
    rows[top + 2] &= ~0x00fe0u;               /* the handle's grip */
    rows[2] |= 7;
    rows[8] |= 7;
    rows[14] |= 7;
    draw_rows(bmp, x, yb, rows);
}

/* A centre-zero bar for the fine tune, +-50 cents to the edges. */
static void draw_fine(void *bmp, int32_t raw, int32_t x, int32_t ytop)
{
    int32_t v = ob1_fine_of(raw) * 100 / 65536 + 50, fill, f0, f1;
    int32_t yb = YB(ytop + 6);
    v = v < 0 ? 0 : v > 100 ? 100 : v;
    fw_framerect(bmp, x, yb - 4, x + KNOB_PX, yb, 1);
    fill = (KNOB_PX * v) / 100;
    f0 = x + 1 + KNOB_PX / 2;
    f1 = x + 1 + KNOB_PX / 2 + fill - KNOB_PX / 2;
    if (f1 < f0) {
        int32_t t = f0;
        f0 = f1;
        f1 = t;
    }
    if (f1 > f0)
        fw_fillrect(bmp, f0, yb - 3, f1, yb - 1, 1);
    fw_fillrect(bmp, x + KNOB_PX / 2, yb - 3, x + 1 + KNOB_PX / 2, yb - 1, 1);
}

/* The knob's icon at panel (x, ytop): 17 x 17, the Digitakt machine's art.
 * `pair` is the knob's partner raw where the art needs it (a WAV knob's
 * TYP, which carries the family). */
static void draw_icon(void *bmp, int32_t k, int32_t raw, int32_t pair,
                      int32_t x, int32_t ytop)
{
    uint32_t rows[KNOB_PX];
    int32_t yb = YB(ytop);
    if (k == K_FRQ1 || k == K_FRQ2) {
        ob1_knob_frq(ob1_frq_of(raw) + 32, rows);
        draw_rows(bmp, x, yb, rows);
    } else if (k == K_TYP1 || k == K_TYP2) {
        ob1_knob_wave(ob1_family_of(raw) ? 127 : 63, rows);
        draw_rows(bmp, x, yb, rows);
    } else if (k == K_WAV1 || k == K_WAV2) {
        ob1_knob_wave(ob1_wave_of(pair, raw), rows);
        draw_rows(bmp, x, yb, rows);
    } else if (k == K_XMOD) {
        ob1_knob_mod(ob1_zone(k, raw, 2) * DSP_XMOD, rows);
        draw_rows(bmp, x, yb, rows);
    } else if (k == K_SYNC) {
        ob1_knob_mod(ob1_zone(k, raw, 2) * DSP_SYNC, rows);
        draw_rows(bmp, x, yb, rows);
    } else if (k == K_FND1 || k == K_OCT1 || k == K_FND2 || k == K_OCT2 ||
               k == K_NOIS) {
        draw_level(bmp, ob1_zone(k, raw, 3), x, ytop);
    } else if (k == K_FIN1 || k == K_FIN2) {
        draw_fine(bmp, raw, x, ytop);
    }
}

/* One cell: the centred 17px icon with the centred label under it (the
 * value in place of the label just after its encoder turned). */
static void draw_cell(void *bmp, int32_t k, int32_t x, int32_t ytop,
                      int32_t w, int32_t value, int32_t pair, int32_t sel,
                      int32_t locked)
{
    char buf[16];
    const char *name = ob1_knobs[k].name;
    const char *text;
    if (!name || !name[0])
        return;
    draw_icon(bmp, k, value >> 8, pair >> 8, x + (w - KNOB_PX) / 2, ytop + 1);
    if (sel) {
        ob1_knobs[k].fmt(value, buf);
        text = buf;
    } else {
        text = name;
    }
    if (locked)
        fw_fillrect(bmp, x, YB(ytop) - 24, x + w, YB(ytop) - 18, -1);
    fw_textbox(bmp, fw_font5, x + w / 2, YB(ytop) - 24, 1, 0, "XXXX", "%s", text);
    if (sel)
        fw_framerect(bmp, x, YB(ytop + 25), x + w, YB(ytop), 1);
}

static int32_t held_step(void)
{
    uint32_t m = ob1_trigs, i;
    if (!m)
        return -1;
    for (i = 0; i < 16; i++)
        if (m & (1u << i))
            return (int32_t)i;
    return -1;
}

/* Lock-table layout (same firmware, same RAM as the proven reference):
 * step row at pattern + 0x1e80 + track*10319 + step*160, word index =
 * sound slot, -1 = none. */
#define LOCK_TAB_OFF    0x1e80u
#define LOCK_TRACK_STR  10319u
#define LOCK_STEP_STR   160u

static int32_t lock_read(uint8_t *pattern, int32_t track, int32_t step,
                         int32_t slot)
{
    uint8_t *row;
    int16_t v;
    if (!pattern || step < 0 || step > 63 || slot < 0 || slot > 78)
        return -1;
    row = pattern + LOCK_TAB_OFF + (uint32_t)track * LOCK_TRACK_STR +
        (uint32_t)step * LOCK_STEP_STR;
    v = *(volatile int16_t *)(row + (uint32_t)slot * 2u);
    return (int32_t)v;
}

static void lock_write(uint8_t *pattern, int32_t track, int32_t step,
                       int32_t slot, int32_t value)
{
    uint8_t *row;
    if (!pattern || step < 0 || step > 63 || slot < 0 || slot > 78)
        return;
    row = pattern + LOCK_TAB_OFF + (uint32_t)track * LOCK_TRACK_STR +
        (uint32_t)step * LOCK_STEP_STR;
    *(volatile int16_t *)(row + (uint32_t)slot * 2u) = (int16_t)value;
}

/* The gate the overlay paints under, and the FUNC+turn reset uses: the
 * active track's sound plays OB-1, the showing key is SYN1/SYN2, no screen
 * sits over the pages and the shown view is on its first page. The reset
 * wants this without the toast hold: a turn still edits its parameter
 * while a copy/paste toast hides the frame. */
static int32_t page_shown(void)
{
    void *bottom, *top, *mgr, *view;
    uint8_t *kit;
    int32_t track, key;
    if (!ob1_brain)
        return 0;
    bottom = *(void *volatile *)((uint8_t *)ob1_brain + BRAIN_BOTTOM);
    top = *(void *volatile *)((uint8_t *)ob1_brain + BRAIN_TOP);
    if (!bottom || bottom != top)
        return 0;
    track = fw_active_track;
    if (track < 0 || track > 3)
        return 0;
    kit = fw_kit;
    if (!kit || (FW_SOUND_SLOT(kit, track, 0) >> 8) != OB1_ID)
        return 0;
    key = *(volatile int32_t *)KEYVAR_AT;
    if (key != 1 && key != 2)
        return 0;
    mgr = *(void *volatile *)((uint8_t *)bottom + SCREEN_MGR);
    if (!mgr)
        return 0;
    view = *(void *volatile *)((uint8_t *)mgr + MGR_VIEW);
    if (!view)
        return 0;
    return *(volatile int32_t *)((uint8_t *)view + VIEW_CUR) == 0;
}

static void shown_state(uint8_t *kit, int32_t track, int32_t page,
                        int32_t *vals, int32_t *lockedmask)
{
    const int8_t *knobs = page == 1 ? ob1_page1_knobs : ob1_page2_knobs;
    int32_t step = held_step();
    int32_t locked = 0;
    uint8_t *pattern = 0;
    int32_t i;
    if (step >= 0 && ob1_edit_rec)
        pattern = fw_pattern;
    for (i = 0; i < 8; i++) {
        int32_t k = knobs[i], slot, v;
        slot = ob1_slot[k];
        v = FW_SOUND_SLOT(kit, track, slot);
        if (step >= 0 && pattern) {
            int32_t lv = lock_read(pattern, track, step, slot);
            if (lv != -1) {
                v = lv;
                locked |= 1 << i;
            }
        }
        vals[i] = v;
    }
    *lockedmask = locked;
}

void ob1_draw(void *bmp, void *ctrl)
{
    int32_t track = fw_active_track, key, page;
    uint8_t *kit;
    int32_t colw, rowh, r, c;
    (void)ctrl;
    ob1_frames++;
    if (!page_shown())
        return;
    if (msg_suppressed())
        return;
    kit = fw_kit;
    key = *(volatile int32_t *)KEYVAR_AT;
    page = key;
    fw_fillrect(bmp, CXC, YB(CY1), CX1, YB(CYT), 0);
    {
        const int8_t *knobs = page == 1 ? ob1_page1_knobs : ob1_page2_knobs;
        int32_t base = page == 1 ? 0 : 8;
        int32_t vals[8], locked = 0;
        shown_state(kit, track, page, vals, &locked);
        colw = (CX1 - CX0 + 1) / COLS;
        rowh = (CY1 - CY0 + 1) / ROWS;
        for (r = 0; r < ROWS; r++) {
            for (c = 0; c < COLS; c++) {
                int32_t li = r * COLS + c;
                int32_t k = knobs[li];
                int32_t x, yt, sel, pair;
                x = CX0 + c * colw;
                yt = CY0 + r * rowh;
                sel = k - base == ob1_enc &&
                    ob1_enc_page == page && ob1_ticks - ob1_enc_at < 45;
                /* A WAV knob's art takes its family from the TYP before it. */
                pair = (k == K_WAV1 || k == K_WAV2) ? vals[li - 1] : 0;
                draw_cell(bmp, k, x, yt, colw - 2, vals[li], pair, sel,
                          (locked >> li) & 1);
            }
        }
    }
}

int ob1_key_handler(void *brain, void *ev)
{
    int32_t id, flags;
    if (brain)
        ob1_brain = brain;
    if (!ev)
        return 0;
    id = *(volatile int32_t *)((const uint8_t *)ev + 12);
    flags = *(volatile int32_t *)((const uint8_t *)ev + 16);
    if (id == 1) {
        if (flags & 0x10)
            ob1_func_down = 0;
        else
            ob1_func_down = 1;
    } else if (id == 4) {
        if (flags & 0x10)
            ob1_ptn_down = 0;
        else
            ob1_ptn_down = 1;
    } else if (id == 19) {
        if (flags & 0x10)
            ob1_page_down = 0;
        else
            ob1_page_down = 1;
    }
    if (id == 10) {
        if (flags & 0x10) {
            ob1_rec_down = 0;
        } else {
            ob1_rec_down = 1;
            if (flags == 1 && !ob1_func_down && !ob1_ptn_down &&
                !ob1_page_down && !ob1_trigs) {
                if (ob1_live) {
                    ob1_live = 0;
                    ob1_edit_rec = 1;
                } else {
                    ob1_edit_rec ^= 1;
                }
            }
        }
    } else if (id == 11 && flags == 1) {
        if (ob1_rec_down) {
            ob1_live = 1;
            ob1_edit_rec = 0;
        } else if (ob1_live) {
            ob1_live = 0;
        }
    } else if (id == 12 && flags == 1) {
        if (ob1_rec_down) {
            ob1_live = 0;
            ob1_edit_rec = 1;
        } else if (ob1_live) {
            ob1_live = 0;
        }
    }
    if (id >= 26 && id <= 41) {
        if (flags & 0x10)
            ob1_trigs &= ~(1u << (uint32_t)(id - 26));
        else
            ob1_trigs |= 1u << (uint32_t)(id - 26);
    } else if (id >= 20 && id <= 25 && (flags & 1)) {
        ob1_trigs = 0;
    }
    if (id >= 42 && id <= 45 && (flags & 1)) {
        ob1_trigs = 0;
        ob1_func_down = 0;
        ob1_ptn_down = 0;
        ob1_page_down = 0;
        ob1_rec_down = 0;
    }
    if ((id == 10 || id == 11 || id == 12) && !(flags & 0x10)) {
        if (ob1_func_down || ob1_ptn_down || ob1_page_down || ob1_trigs ||
            ((id == 11 || id == 12) && ob1_rec_down))
            ob1_msg_until = ob1_ticks + OB1_MSG_HOLD;
    }
    return 0;
}

int ob1_enc_handler(void *brain, void *ev)
{
    int32_t enc, delta, key, k;
    if (brain)
        ob1_brain = brain;
    if (!ev)
        return 0;
    enc = *(volatile int32_t *)((const uint8_t *)ev + 12);
    delta = *(volatile int32_t *)((const uint8_t *)ev + 16);
    (void)delta;
    if (enc < 1 || enc > 8)
        return 0;
    key = *(volatile int32_t *)KEYVAR_AT;
    if (key != 1 && key != 2)
        return 0;
    ob1_enc = enc - 1;
    ob1_enc_page = key;
    ob1_enc_at = ob1_ticks;
    /* A FUNC+turn on a knob: the stock reset would write the borrowed
     * stock record's default, which is FM's (DECA/DECB 32 -> -25.0 cents
     * on FIN1/FIN2, ATKA/ATKB 0 -> -32 semitones on FRQ1/FRQ2), not OB-1's.
     * Write OB-1's own default where the turn edits, the sound or a held
     * step's lock, and take the event so the stock reset does not run. */
    if (ob1_func_down) {
        k = (key == 1 ? ob1_page1_knobs : ob1_page2_knobs)[enc - 1];
        if (k >= 0 && ob1_defaults[k] != -1 && page_shown()) {
            int32_t track = fw_active_track, step = held_step();
            int32_t slot = ob1_slot[k], def = ob1_defaults[k];
            uint8_t *pattern = (step >= 0 && ob1_edit_rec) ? fw_pattern : 0;
            if (pattern)
                lock_write(pattern, track, step, slot, def);
            else
                core_sound_set(track, slot, def);
            return 1;
        }
    }
    return 0;
}

static int32_t tick_valid;
static int32_t tick_track = -1;
static int32_t tick_key = -1;
static int32_t tick_step = -1;
static int32_t tick_vals[8];
static int32_t tick_locked;
static int32_t tick_enc = -1;
static int32_t tick_enc_page = -1;
static int32_t tick_hl;

void ob1_tick(void *ctrl)
{
    int32_t track = fw_active_track, mid, key, page, step, i, hl, locked = 0;
    int32_t dirty = 0;
    int32_t vals[8];
    uint8_t *kit;
    int32_t n;
    ob1_ticks++;
    if (!ctrl)
        return;
    if (ob1_brain) {
        void *bottom = *(void *volatile *)((uint8_t *)ob1_brain + BRAIN_BOTTOM);
        void *top = *(void *volatile *)((uint8_t *)ob1_brain + BRAIN_TOP);
        if (!bottom || bottom != top) {
            ob1_trigs = 0;
            ob1_func_down = 0;
            ob1_ptn_down = 0;
            ob1_page_down = 0;
            ob1_rec_down = 0;
        }
    }
    ob1_reap();
    if (track < 0 || track > 3)
        return;
    kit = fw_kit;
    if (!kit)
        return;
    mid = FW_SOUND_SLOT(kit, track, 0) >> 8;
    if (mid != OB1_ID) {
        tick_valid = 0;
        return;
    }
    key = *(volatile int32_t *)KEYVAR_AT;
    if (key != 1 && key != 2) {
        tick_valid = 0;
        return;
    }
    if (msg_suppressed()) {
        tick_valid = 0;
        return;
    }
    page = key;
    n = 8;
    step = held_step();
    shown_state(kit, track, page, vals, &locked);
    hl = ob1_enc >= 0 && ob1_enc_page == page &&
        ob1_ticks - ob1_enc_at < 45;
    if (!tick_valid || track != tick_track || key != tick_key ||
        step != tick_step || locked != tick_locked ||
        ob1_enc != tick_enc || ob1_enc_page != tick_enc_page || hl != tick_hl)
        dirty = 1;
    else {
        for (i = 0; i < n; i++) {
            if (vals[i] != tick_vals[i]) {
                dirty = 1;
                break;
            }
        }
    }
    if (!dirty)
        return;
    tick_valid = 1;
    tick_track = track;
    tick_key = key;
    tick_step = step;
    tick_locked = locked;
    for (i = 0; i < n; i++)
        tick_vals[i] = vals[i];
    tick_enc = ob1_enc;
    tick_enc_page = ob1_enc_page;
    tick_hl = hl;
    *((volatile uint8_t *)ctrl + 0x20) = 1;
}
