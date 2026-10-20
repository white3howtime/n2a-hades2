# Sanity check for patch_nso.py output: after rewriting, the original text must not contain x18/w18, svc, counter reads or
# tpidr accesses anywhere except the dead originals of exclusive-access windows (whose first instruction becomes a branch),
# and the patch area must not use x18/svc/counter reads.
# usage: check_patched_nso.py <module dir> <name>...      (reads <name>.patched.bin, .fixups.tsv, .sites.tsv)
import re, struct, sys
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM

md = Cs(CS_ARCH_ARM64, CS_MODE_ARM); md.skipdata = True
FORBID_X18 = re.compile(r'\b[xw]18\b')

def forbidden(ins, in_text):
    if FORBID_X18.search(ins.op_str) or ins.mnemonic == 'svc': return True
    if ins.mnemonic == 'mrs' and ('cntpct' in ins.op_str or 'cntvct' in ins.op_str): return True
    return in_text and ins.mnemonic in ('mrs', 'msr') and 'tpidr' in ins.op_str

def check(d, name):
    hdr = {}
    for l in open(f'{d}/{name}.fixups.tsv'):
        m = re.match(r'# (\w+)\t(\S+)', l)
        if m: hdr[m.group(1)] = m.group(2)
    text_end, patch_off = int(hdr['TEXT_END'], 16), int(hdr['PATCH_OFF'], 16)
    img = open(f'{d}/{name}.patched.bin', 'rb').read()
    wins = [(int(p[0], 16), int(p[2], 16)) for p in (l.split() for l in open(f'{d}/{name}.sites.tsv')) if len(p) >= 3 and p[1] == 'W']
    problems = [f'window head {lo:#x} is not a branch' for lo, _ in wins if struct.unpack_from('<I', img, lo)[0] & 0xFC000000 != 0x14000000]
    for ins in md.disasm(img[:text_end], 0):
        if forbidden(ins, True) and not any(lo <= ins.address < hi for lo, hi in wins):
            problems.append(f'text {ins.address:#x} {ins.mnemonic} {ins.op_str}')
    for ins in md.disasm(img[patch_off:], patch_off):
        if forbidden(ins, False): problems.append(f'patch {ins.address:#x} {ins.mnemonic} {ins.op_str}')
    print(f'{name}: {len(wins)} windows, {len(problems)} problems')
    for p in problems[:20]: print('  ', p)
    return not problems

if __name__ == '__main__':
    ok = all([check(sys.argv[1], n) for n in sys.argv[2:]])
    sys.exit(0 if ok else 1)
