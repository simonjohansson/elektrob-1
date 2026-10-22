| SPDX-License-Identifier: GPL-2.0-or-later
| OB-1 Digitone pages.s — no page-constructor patches.
|
| OB-1 borrows stock SYN ranges only (no param:182-184 minted), so no
| knob slot is rerouted and no jsr site is needed on any OS version. This
| file stays an empty translation unit so mod.json's sources list keeps
| its shape if a future mint needs a home here.
| ColdFire V4; assemble with -mcpu=54455, like core-dn1.
        .section .run, "ax"
        .globl  ob1_pages_anchor
ob1_pages_anchor:
        rts
