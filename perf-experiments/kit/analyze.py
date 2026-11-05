"""Summarize PR2 A/B runs: steady-scene FPS, frame spikes, memory and background builds."""
import csv
import statistics
import sys
from pathlib import Path

root = Path(__file__).parent / 'runs'
prefix = sys.argv[1] if len(sys.argv) > 1 else 'smo'
lo, hi = (int(sys.argv[2]), int(sys.argv[3])) if len(sys.argv) > 3 else (85000, 148000)


def pct(values, q):
    values = sorted(values)
    return values[min(len(values) - 1, int(round(q * (len(values) - 1))))]


rows = []
for run in sorted(root.glob(f'{prefix}-*')):
    perf = list(csv.DictReader(open(run / 'perf.csv')))
    # The memory log was written with a French locale: "ms,ws_int,ws_frac,priv_int,priv_frac,busy".
    mem = []
    for line in open(run / 'memory.csv').read().splitlines()[1:]:
        f = line.split(',')
        mem.append({'working_set_mb': f'{f[1]}.{f[2]}', 'private_mb': f'{f[3]}.{f[4]}',
                    'build_processes': f[5] if len(f) > 5 else '0', 'ms': f[0]})
    mem = [m for m in mem if float(m['working_set_mb']) > 100]
    steady = [r for r in perf if lo <= int(r['ms']) <= hi and float(r['fps']) > 0]
    fps = [float(r['fps']) for r in steady]
    ft = [float(r['frametime_ms']) if 'nan' not in r['frametime_ms'] else 0.0 for r in steady]
    longest = [float(r['longest_ms']) for r in steady]
    building = sum(int(r['shaders_building']) > 0 for r in perf)
    busy = sum(int(r.get('build_processes', 0) or 0) > 0 for r in mem)
    ws = max(float(r['working_set_mb']) for r in mem)
    priv_end = max(float(r["private_mb"]) for r in mem)
    spikes = sum(v > 50 for r in perf for v in [float(r['longest_ms'])])
    rows.append((run.name, len(fps), statistics.mean(fps), statistics.median(fps), pct(fps, 0.05),
                 statistics.mean(ft), statistics.median(longest), pct(longest, 0.95), max(longest),
                 spikes, building, ws, priv_end, busy))

print(f'steady window {lo}-{hi} ms')
print(f"{'run':22} {'n':>3} {'fps':>7} {'p50':>7} {'p5':>7} {'ft_ms':>6} {'long50':>6} {'long95':>6} {'longMax':>7} {'>50ms':>5} {'shdr_s':>6} {'ws_max':>7} {'priv':>7} {'busy_s':>6}")
for r in rows:
    print(f'{r[0]:22} {r[1]:3d} {r[2]:7.1f} {r[3]:7.1f} {r[4]:7.1f} {r[5]:6.3f} {r[6]:6.2f} {r[7]:6.2f} {r[8]:7.1f} {r[9]:5d} {r[10]:6d} {r[11]:7.0f} {r[12]:7.0f} {r[13]:6d}')
for variant in ('A-master', 'B-pr2'):
    sel = [r for r in rows if r[0].endswith(variant) and r[13] == 0]
    if sel:
        print(f'{variant}: runs without background builds {len(sel)}, mean fps {statistics.mean(r[2] for r in sel):.1f}, '
              f'frametime {statistics.mean(r[5] for r in sel):.3f} ms, long95 {statistics.mean(r[7] for r in sel):.2f} ms, '
              f'ws_max {statistics.mean(r[11] for r in sel):.0f} MB')
