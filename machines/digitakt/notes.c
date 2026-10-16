/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "digitakt-mk1/core3.h"
#include "notes.h"

extern volatile int32_t ob1_stock_held[8 * 128];
extern void ob1_stock_note(const struct ob1_note *note);

static struct {
    struct ob1_note note[128];
    uint8_t order[128];
    int32_t count;
} held[8];

static uint16_t lock(void)
{
    uint16_t sr = 0;
#ifdef __m68k__
    __asm__ volatile("move.w %%sr,%0\n\tmove.w #0x2700,%%sr" : "=d"(sr) :: "memory", "cc");
#endif
    return sr;
}

static void unlock(uint16_t sr)
{
#ifdef __m68k__
    __asm__ volatile("move.w %0,%%sr" :: "d"(sr) : "memory", "cc");
#else
    (void)sr;
#endif
}

void ob1_notes_clear(int32_t track)
{
    uint16_t sr = lock();
    if ((uint32_t)track < 8)
        held[track].count = 0;
    unlock(sr);
}

void ob1_live_note(const struct ob1_note *event)
{
    int32_t t = event->track, n = event->note, i, count, top;
    struct ob1_note recall;
    uint16_t sr = lock();

    if ((uint32_t)t >= 8 || (uint32_t)n >= 128)
        goto stock;
    if (!fw_kit || FW_SOUND(fw_kit, t)[FW_SOUND_MACHINE] != 69) {
        held[t].count = 0;
        goto stock;
    }
    if (event->type != 2 && (event->type != 1 || !(event->flags & 0x10000)))
        goto stock;

    count = held[t].count;
    top = count ? held[t].order[count - 1] : -1;
    held[t].count = 0;
    for (i = 0; i < count; i++) {
        int32_t key = held[t].order[i];
        /* Follow stock count resets rather than reviving stale holds. */
        if (ob1_stock_held[128 * t + key] > 0 && (event->type != 1 || key != n))
            held[t].order[held[t].count++] = (uint8_t)key;
    }
    if (event->type == 1) {
        held[t].note[n] = *event;
        held[t].order[held[t].count++] = (uint8_t)n;
        goto stock;
    }
    if (!held[t].count)
        goto stock;
    if (top != n || ob1_stock_held[128 * t + n] > 0) {
        unlock(sr);
        return;
    }
    recall = held[t].note[held[t].order[held[t].count - 1]];
    event = &recall;
stock:
    /* Keep stack selection and enqueue ordered across UI and MIDI tasks. */
    ob1_stock_note(event);
    unlock(sr);
}
