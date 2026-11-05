"""TEMP: which Maxwell3D registers change between draws (NXEMU_TEMP_REGLOG output)."""
import re, struct, sys
from collections import Counter

log, header = sys.argv[1], sys.argv[2]
pos = sorted((int(p, 16), n) for n, p in re.findall(r'ASSERT_REG_POSITION\((\w+(?:\.\w+)*),\s*(0x[0-9A-Fa-f]+)\)', open(header).read()))
SPECIAL = {0xFFFF: '<cb bind size/enable>', 0xFFFE: '<topology>', 0xFFFD: '<engine_state>',
           0xFFFC: '<shaders dirty>', 0xFFFB: '<pipeline changed>', 0xFFFA: '<channel>'}

def name(m):
    if m in SPECIAL:
        return SPECIAL[m]
    best = None
    for p, n in pos:
        if p <= m:
            best = (p, n)
    return f'{best[1]}+{m - best[0]:#x}' if best and m != best[0] else (best[1] if best else hex(m))

def group(m):
    if m in SPECIAL:
        return SPECIAL[m]
    best = None
    for p, n in pos:
        if p <= m:
            best = n
    return best

data = open(log, 'rb').read()
draws, i = [], 0
while i + 2 <= len(data):
    (n,) = struct.unpack_from('<H', data, i); i += 2
    draws.append(set(struct.unpack_from(f'<{n}H', data, i))); i += 2 * n
total = len(draws)
print(f'{total} draws')
per_group = Counter()
for d in draws:
    for g in {group(m) for m in d}:
        per_group[g] += 1
print('draws in which a register group changed:')
for g, c in per_group.most_common(70):
    print(f'{c / total:7.1%}  {g}')
empty = sum(1 for d in draws if not d)
print(f'no change at all: {empty / total:.1%}')
same_pipe = sum(1 for d in draws if 0xFFFB not in d)
print(f'same pipeline as previous draw: {same_pipe / total:.1%}')
# Hit rate if some groups are ignored
if len(sys.argv) > 3:
    ignored = set(open(sys.argv[3]).read().split())
    hit = sum(1 for d in draws if all(group(m) in ignored for m in d if m != 0xFFFB))
    print(f'fast path hits with ignore list: {hit / total:.1%}')
    wrong = sum(1 for d in draws if 0xFFFB in d and all(group(m) in ignored for m in d if m != 0xFFFB))
    print(f'  of which pipeline actually changed (must be 0): {wrong}')
