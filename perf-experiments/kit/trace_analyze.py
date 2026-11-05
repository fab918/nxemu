"""TEMP: frame timeline analysis for the FrameTrace JSON files of one run."""
import glob, json, sys
from collections import defaultdict

folder = sys.argv[1]
events = []
for path in glob.glob(folder + '/trace-*.json'):
    events += json.load(open(path))['traceEvents']

spans = defaultdict(list)    # (name, tid) -> [(begin, end)]
instants = defaultdict(list) # name -> [(ts, tid, arg)]
tids_by_name = defaultdict(set)
for e in events:
    if e['ph'] == 'X':
        spans[(e['name'], e['tid'])].append((e['ts'], e['ts'] + e['dur']))
    else:
        instants[e['name']].append((e['ts'], e['tid'], e['args']['a']))
    tids_by_name[e['name']].add(e['tid'])

def merged(intervals):
    out = []
    for b, e in sorted(tuple(x) for x in intervals):
        if out and b <= out[-1][1]:
            out[-1][1] = max(out[-1][1], e)
        else:
            out.append([b, e])
    return out

def clip(intervals, lo, hi):
    return [(max(b, lo), min(e, hi)) for b, e in intervals if e > lo and b < hi]

def total(intervals):
    return sum(e - b for b, e in intervals)

def intersect(a, b):
    out, i, j = [], 0, 0
    while i < len(a) and j < len(b):
        lo, hi = max(a[i][0], b[j][0]), min(a[i][1], b[j][1])
        if lo < hi:
            out.append((lo, hi))
        if a[i][1] < b[j][1]:
            i += 1
        else:
            j += 1
    return out

def complement(intervals, lo, hi):
    out, cur = [], lo
    for b, e in intervals:
        if b > cur:
            out.append((cur, b))
        cur = max(cur, e)
    if cur < hi:
        out.append((cur, hi))
    return out

presents = sorted(b for (n, t), v in spans.items() if n == 'present' for b, _ in v)
gpu_tid = next(iter(tids_by_name['gpu.wait_cmd']))
worker_tids = tids_by_name['vk.wait_work']
core_tids = sorted(tids_by_name['core.idle'])
lo, hi = presents[0], presents[-1]
frames = len(presents) - 1
ms = lambda us: us / 1000.0
print(f'window {ms(hi - lo):.1f} ms, {frames} frames, {ms(hi - lo) / frames:.2f} ms/frame')
print(f'gpu thread {gpu_tid}, worker {sorted(worker_tids)}, cores {core_tids}')

def per_frame(intervals):
    return ms(total(clip(merged(intervals), lo, hi))) / frames

gpu_wait_cmd = merged(clip(spans[('gpu.wait_cmd', gpu_tid)], lo, hi))
gpu_waits = {n: per_frame(spans[(n, gpu_tid)]) for n in ('gpu.wait_cmd', 'gpu.wait_worker', 'gpu.finish', 'wait_fence', 'present')}
print('GPU thread per frame (ms):', {k: round(v, 3) for k, v in gpu_waits.items()})
gpu_blocked = merged(sum((spans[(n, gpu_tid)] for n in ('gpu.wait_cmd', 'gpu.wait_worker', 'gpu.finish', 'wait_fence')), []))
gpu_busy = complement(merged(clip(gpu_blocked, lo, hi)), lo, hi)
print(f'GPU thread busy {ms(total(gpu_busy)) / frames:.3f} ms/frame')
for w in sorted(worker_tids):
    print(f'worker {w} waiting for work {per_frame(spans[("vk.wait_work", w)]):.3f} ms/frame, fence waits {per_frame(spans[("wait_fence", w)]):.3f}')

core_idle = {t: merged(clip(spans[('core.idle', t)], lo, hi)) for t in core_tids}
for t in core_tids:
    print(f'core {t} idle {ms(total(core_idle[t])) / frames:.3f} ms/frame')
all_idle = core_idle[core_tids[0]]
for t in core_tids[1:]:
    all_idle = intersect(all_idle, core_idle[t])
any_busy = complement(all_idle, lo, hi)
print(f'all cores idle {ms(total(all_idle)) / frames:.3f} ms/frame')

bubble = intersect(gpu_wait_cmd, all_idle)
guest_bound = intersect(gpu_wait_cmd, any_busy)
gpu_only = intersect(gpu_busy, all_idle)
both = intersect(gpu_busy, any_busy)
print('--- per frame (ms) ---')
print(f'GPU thread waits for commands while every core idles (bubble): {ms(total(bubble)) / frames:.3f}')
print(f'GPU thread waits for commands while a core runs (guest bound):  {ms(total(guest_bound)) / frames:.3f}')
print(f'GPU thread works while every core idles (GPU bound):          {ms(total(gpu_only)) / frames:.3f}')
print(f'GPU thread and a core both work (overlap):                    {ms(total(both)) / frames:.3f}')
gpu_other_wait = complement(merged(gpu_wait_cmd + gpu_busy), lo, hi)
print(f'GPU thread blocked on worker/fence:                           {ms(total(gpu_other_wait)) / frames:.3f}')

# What happens around the bubbles: longest ones and the instants that end them
bubbles = sorted(bubble, key=lambda x: x[1] - x[0], reverse=True)[:8]
marks = sorted((ts, name, arg) for name, v in instants.items() for ts, tid, arg in v)
print('--- longest bubbles ---')
for b, e in bubbles:
    near = [f'{n}@{(ts - b) / 1000:+.2f}' + (f'({a >> 32}:{a & 0xffffffff})' if n == 'guest.event_wait' else f'({a})' if n.startswith('sp.') else '')
            for ts, n, a in marks if b - 300 <= ts <= e + 300][:12]
    print(f'{ms(e - b):.3f} ms at {ms(b - lo):.1f}: {" ".join(near)}')

counts = {n: len(v) / frames for n, v in instants.items()}
print('instants per frame:', {k: round(v, 1) for k, v in sorted(counts.items())})
