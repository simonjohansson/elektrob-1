/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef OB1_DIGITAKT_NOTES_H
#define OB1_DIGITAKT_NOTES_H

#include <stdint.h>

/* Stock live-note request; retain the opaque trigger and sound fields on recall. */
struct ob1_note {
    int32_t track, note, velocity, type;
    uint32_t flags;
    int32_t tail[7];
};

void ob1_live_note(const struct ob1_note *note);
void ob1_notes_clear(int32_t track);

#endif
