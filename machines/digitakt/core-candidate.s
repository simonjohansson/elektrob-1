| SPDX-License-Identifier: GPL-2.0-or-later
| MIDI task: int32_t handler(int32_t track, int32_t machine, int32_t cc).
| Track is the resolved native source: 0-7 audio, 8-15 MIDI, 16 FX.
| First nonzero parameter ID replaces lookup; zero delegates to stock.
| With no subscriber the original lookup, including core's machine bank, runs.
        .globl core_midi_cc_parameter
core_midi_cc_parameter:
        move.l  %a2, -(%sp)
        lea     ev_midi_cc_parameter, %a2
1:      move.l  (%a2)+, %d0
        beq.s   3f
        movea.l %d0, %a0
        move.l  16(%sp), -(%sp)
        move.l  16(%sp), -(%sp)
        move.l  16(%sp), -(%sp)
        jsr     (%a0)
        lea     12(%sp), %sp
        tst.l   %d0
        beq.s   1b
        movea.l (%sp)+, %a2
        rts
3:      movea.l (%sp)+, %a2
        move.l  %d2, -(%sp)
        moveq   #127, %d2
        move.l  8(%sp), %d0
        jmp     0x400791a6
