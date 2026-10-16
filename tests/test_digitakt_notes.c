/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "digitakt-mk1/core3.h"
#include "../machines/digitakt/notes.h"

static uint8_t kit[0x20 + 8 * 0xa2];
uint8_t *volatile fw_kit = kit;
volatile int32_t ob1_stock_held[8 * 128];
static struct ob1_note output;
static int calls;

void ob1_stock_note(const struct ob1_note *note)
{
    output = *note;
    calls++;
}

static void reset(void)
{
    int t;
    memset(kit, 0, sizeof(kit));
    memset((void *)ob1_stock_held, 0, sizeof(ob1_stock_held));
    for (t = 0; t < 8; t++) {
        FW_SOUND(kit, t)[FW_SOUND_MACHINE] = 69;
        ob1_notes_clear(t);
    }
    calls = 0;
}

static void event(int track, int note, int on)
{
    struct ob1_note e = {track, note, 70 + note % 50, on ? 1 : 2,
                        on ? 0x10001u : 0, {0}};
    e.tail[4] = 1234 + note;
    if (on)
        ob1_stock_held[128 * track + note]++;
    else if (ob1_stock_held[128 * track + note])
        ob1_stock_held[128 * track + note]--;
    ob1_live_note(&e);
}

int main(void)
{
    int i, a, b, c, before;
    reset();
    for (i = 60; i < 64; i++) event(0, i, 1);
    for (i = 63; i > 60; i--) {
        event(0, i, 0);
        assert(output.type == 1 && output.note == i - 1);
        assert(output.velocity == 70 + (i - 1) % 50);
        assert(output.flags == 0x10001 && output.tail[4] == 1234 + i - 1);
    }
    event(0, 60, 0);
    assert(output.type == 2 && output.note == 60 && calls == 8);

    /* Every release permutation: only the newest remaining hold can sound. */
    for (a = 0; a < 3; a++) for (b = 0; b < 3; b++) for (c = 0; c < 3; c++) {
        int order[3] = {a, b, c}, down[3] = {1, 1, 1};
        if (a == b || a == c || b == c) continue;
        reset();
        for (i = 0; i < 3; i++) event(0, 60 + i, 1);
        for (i = 0; i < 3; i++) {
            int last = 2;
            down[order[i]] = 0;
            event(0, 60 + order[i], 0);
            while (last >= 0 && !down[last]) last--;
            assert(output.type == (last < 0 ? 2 : 1));
            if (last >= 0) assert(output.note == 60 + last);
        }
    }
    reset();
    event(0, 60, 1); event(0, 62, 1); event(0, 60, 1);
    before = calls;
    event(0, 60, 0); /* Another press of this pitch is still held. */
    assert(calls == before);
    event(0, 60, 0);
    assert(output.type == 1 && output.note == 62);
    event(0, 99, 0); /* An unmatched release cannot silence the current hold. */
    assert(output.type == 1 && output.note == 62);
    event(0, 62, 0);
    assert(output.type == 2);

    reset();
    for (i = 0; i < 128; i++) event(0, i, 1);
    for (i = 127; i >= 0; i--) {
        event(0, i, 0);
        assert(output.type == (i ? 1 : 2));
        if (i) assert(output.note == i - 1);
    }
    reset();
    event(0, 60, 1); event(1, 65, 1); event(0, 62, 1);
    event(0, 62, 0);
    assert(output.track == 0 && output.note == 60);
    event(1, 65, 0);
    assert(output.track == 1 && output.type == 2);

    reset();
    event(0, 60, 1); event(0, 62, 1);
    memset((void *)ob1_stock_held, 0, sizeof(ob1_stock_held)); /* Stock panic/reset. */
    event(0, 64, 1); event(0, 64, 0);
    assert(output.type == 2);
    event(0, 60, 1); ob1_notes_clear(0); event(0, 62, 1); event(0, 62, 0);
    assert(output.type == 2); /* Machine re-entry must not revive its old holds. */

    reset();
    FW_SOUND(kit, 0)[FW_SOUND_MACHINE] = 3;
    event(0, 60, 1); event(0, 62, 1); event(0, 62, 0);
    assert(output.type == 2 && calls == 3);
    puts("Digitakt held-note priority: OK");
    return 0;
}
