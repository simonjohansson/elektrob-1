# SPDX-License-Identifier: GPL-2.0-or-later
"""Run tools/bench.c's bench() for the ColdFire in Unicorn and count its instructions a block.
bench.py BENCH.bin BENCH.elf: the image linked at 0x10000 and the ELF for its symbols (m68k-elf-nm)."""
import subprocess
import sys

from capstone import CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_040, Cs
from unicorn import UC_ARCH_M68K, UC_HOOK_CODE, UC_MODE_BIG_ENDIAN, Uc
from unicorn.m68k_const import UC_CPU_M68K_CFV4E, UC_M68K_REG_A7

BASE, STACK, RET = 0x10000, 0x200000, 0x1f0000
WARM, BLOCKS = 4, 16
# the stock render takes about 84,000 instructions a block, 80 % of it (Digi Mono's measurement on a unit)
STOCK, STOCK_LOAD = 84000, 0.80

image = open(sys.argv[1], 'rb').read()
syms = {l.split()[2]: int(l.split()[0], 16) for l in subprocess.check_output(
    ['m68k-elf-nm', sys.argv[2]], text=True).splitlines() if len(l.split()) == 3}
cs = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_040)


def run(blocks, voices, setting):
    uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_CPU_M68K_CFV4E)
    uc.mem_map(0, 0x400000)
    uc.mem_write(BASE, image)
    sp = STACK - 16
    uc.mem_write(sp, RET.to_bytes(4, 'big') + blocks.to_bytes(4, 'big') + voices.to_bytes(4, 'big')
                 + setting.to_bytes(4, 'big'))
    uc.reg_write(UC_M68K_REG_A7, sp)
    count = {'all': 0, 'mul': 0, 'div': 0}
    seen = {}

    def hook(u, addr, size, _):
        count['all'] += 1
        kind = seen.get(addr)
        if kind is None:
            ins = next(cs.disasm(bytes(u.mem_read(addr, size)), addr), None)
            m = ins.mnemonic if ins else ''
            kind = seen[addr] = 'mul' if m.startswith('mul') else 'div' if m.startswith(('div', 'rem')) else ''
        if kind:
            count[kind] += 1

    uc.hook_add(UC_HOOK_CODE, hook)
    uc.emu_start(syms['bench'], RET)
    return count


print('%-44s %9s %9s %6s %6s %s' % ('setting', 'instr/blk', 'per voice', 'muls', 'divs', '8 voices vs the stock render'))
for setting, name in ((1, 'default: one saw'), (2, 'both VCOs and subs'), (0, 'worst: + SYNC, XMOD, noise')):
    warm = run(WARM, 8, setting)
    full = run(WARM + BLOCKS, 8, setting)
    per = {k: (full[k] - warm[k]) / BLOCKS for k in full}
    share = per['all'] / (STOCK / STOCK_LOAD)
    print('%-44s %9.0f %9.0f %6.0f %6.0f %+.0f %% of a block (stock alone: %.0f %%)' % (
        name, per['all'], per['all'] / 8, per['mul'], per['div'], 100 * share, 100 * STOCK_LOAD))
