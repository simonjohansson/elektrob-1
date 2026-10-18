| SPDX-License-Identifier: GPL-2.0-or-later
| Native SRC value lookup includes the view's held-step parameter locks.
        .section .run, "ax"
        .globl ob1_view_value
ob1_view_value:
        jmp OB1_VIEW_VALUE
