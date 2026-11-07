# Sequencer tracks mod (v0.4.3) - PolyForm Noncommercial 1.0.0 (see seqgen/LICENSE.md). Host tool, not part of `make test`.
# Approximate Cortex-M4 cycle counter for ARM code built like the firmware (-O2, -mcpu=cortex-m4, hard float): unicorn executes the
# ELF, capstone classifies every basic block once (1 cycle per instruction, loads 2, ldm/stm/push/pop 1 + N, vdiv 14, udiv 7, vmla 3,
# +2 per taken branch). SDRAM accesses (the .sdram section at 0x80000000) are counted separately. The numbers rank costs, they are
# not exact device cycles (no flash wait states / cache, no bus contention).
# Needs: pip install unicorn capstone.   Usage:
#   sh build.sh bench_plinky.cpp ../.. plinky.elf && python emu.py plinky.elf 0,7,1,2,3,8,5,6 [p]   (p = per function profile)
# Scenarios (bench_plinky.cpp): 0 = 1 instance 4 voices, 7 = same + sends, 1 = 3 instances 8 voices + sends, 2 = 1 + chorus,
# 3 = 1 + sub + SVF, 8 = 1 + sub + ladder, 4 = 1 + filter envelope, 5 = 1 + wavetable, 6 = 1 mono.
import sys, subprocess, re
from unicorn import *
from unicorn.arm_const import *
import capstone as cs

def syms(elf):
    out = subprocess.run(['arm-none-eabi-nm', elf], capture_output=True, text=True).stdout
    d = {}
    for l in out.splitlines():
        p = l.split()
        if len(p) == 3: d[p[2]] = int(p[0], 16)
    return d

def load(uc, elf):
    import struct
    data = open(elf, 'rb').read()
    # minimal ELF32 program header loader
    e_phoff, = struct.unpack_from('<I', data, 0x1c)
    e_phentsize, e_phnum = struct.unpack_from('<HH', data, 0x2a)
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz = struct.unpack_from('<IIIIII', data, off)
        if p_type == 1 and p_filesz:
            uc.mem_write(p_vaddr, data[p_offset:p_offset + p_filesz])

md = cs.Cs(cs.CS_ARCH_ARM, cs.CS_MODE_THUMB | cs.CS_MODE_MCLASS)
md.detail = False
cache = {}
def block_cost(uc, addr, size):
    k = (addr, size)
    c = cache.get(k)
    if c is None:
        code = bytes(uc.mem_read(addr, size))
        c = 0; n = 0
        for ins in md.disasm(code, addr):
            n += 1
            m = ins.mnemonic.split('.')[0]
            ops = ins.op_str
            if m in ('push', 'pop', 'vpush', 'vpop') or m.startswith('ldm') or m.startswith('stm') or m.startswith('vldm') or m.startswith('vstm'):
                regs = ops[ops.find('{') + 1:ops.find('}')]
                cnt = 0
                for r in regs.split(','):
                    r = r.strip()
                    if '-' in r:
                        a, b = r.split('-'); cnt += int(b[1:]) - int(a[1:]) + 1
                    elif r: cnt += 1
                c += 1 + cnt
                if m == 'pop' and 'pc' in ops: c += 2
            elif m.startswith('ldr') or m == 'vldr':
                c += 2
            elif m in ('vdiv', 'vsqrt'):
                c += 14
            elif m in ('udiv', 'sdiv'):
                c += 7
            elif m in ('vmla', 'vmls', 'vnmla', 'vnmls', 'vfma', 'vfms'):
                c += 3
            else:
                c += 1
        cache[k] = c = (c, n)
    return c

class Counter:
    def __init__(s): s.fmap = None; s.per = {}; s.cyc = 0; s.ins = 0; s.last_end = None; s.sd = 0; s.sdr = 0; s.sdw = 0
    on = False
    def hook(s, uc, addr, size, ud):
        if not s.on: return
        c, n = block_cost(uc, addr, size)
        s.cyc += c; s.ins += n
        if s.fmap is not None:
            f = s.fmap(addr); s.per[f] = s.per.get(f, 0) + c
        if s.last_end is not None and addr != s.last_end: s.cyc += 2  # taken branch refill
        s.last_end = addr + size
    def sdhook(s, uc, access, addr, size, value, ud):
        if s.on:
            s.sd += 1
            if access == UC_MEM_READ: s.sdr += 1
            else: s.sdw += 1

def make(elf):
    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    try:
        uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M4)
    except Exception: pass
    uc.mem_map(0, 0x100000)
    uc.mem_map(0x20000000, 0x40000)
    uc.mem_map(0x80000000, 0x100000)
    uc.mem_map(0xE0000000, 0x100000)
    load(uc, elf)
    return uc

SENT = 0x000FFF00
def call(uc, sy, name, arg=0, ctr=None):
    uc.reg_write(UC_ARM_REG_SP, 0x2003FF00)
    uc.reg_write(UC_ARM_REG_R0, arg)
    uc.reg_write(UC_ARM_REG_LR, SENT | 1)
    uc.emu_start(sy[name] | 1, SENT, count=0)
    return uc.reg_read(UC_ARM_REG_R0)

import bisect
def funcmap(elf):
    out = subprocess.run(['arm-none-eabi-nm', '-C', '-n', elf], capture_output=True, text=True).stdout
    a = []; nm = []
    for l in out.splitlines():
        p = l.split(None, 2)
        if len(p) == 3 and p[1] in 'tTWw': a.append(int(p[0], 16) & ~1); nm.append(p[2][:70])
    def f(x):
        i = bisect.bisect_right(a, x) - 1
        return nm[i] if i >= 0 else '?'
    return f
PROF = False
def bench(elf, sc, warm=4, n=16):
    sy = syms(elf)
    uc = make(elf)
    if 'g_sc' in sy: uc.mem_write(sy['g_sc'], sc.to_bytes(4, 'little'))
    ctr = Counter()
    if PROF: ctr.fmap = funcmap(elf)
    h = uc.hook_add(UC_HOOK_BLOCK, ctr.hook)
    h2 = uc.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, ctr.sdhook, begin=0x80000000, end=0x800FFFFF)
    call(uc, sy, 'setup', sc)
    call(uc, sy, 'run', warm)
    ctr.on = True
    call(uc, sy, 'run', n)
    ctr.on = False
    v = call(uc, sy, 'voices')
    hs = call(uc, sy, 'hash')
    if PROF:
        for k, c in sorted(ctr.per.items(), key=lambda x: -x[1])[:8]: print('   %8.0f  %s' % (c / n, k))
    bench.last = (ctr.sdr / n, ctr.sdw / n)
    return ctr.cyc / n, ctr.ins / n, ctr.sd / n, v, hs

if __name__ == '__main__':
    elf = sys.argv[1]
    PROF = len(sys.argv) > 3
    for sc in [int(x) for x in sys.argv[2].split(',')]:
        cyc, ins, sd, v, hs = bench(elf, sc)
        print('%s sc%d voices %d: %8.0f cyc/block (%.1f%% of 522k), %7.0f instr, %5.0f sdram acc (%d R, %d W), hash %08x; with SDRAM stalls (20 R, 10 W): %.1f%%' % (elf, sc, v, cyc, cyc / 5222.4, ins, sd, bench.last[0], bench.last[1], hs & 0xffffffff, (cyc + 20 * bench.last[0] + 10 * bench.last[1]) / 5222.4))
