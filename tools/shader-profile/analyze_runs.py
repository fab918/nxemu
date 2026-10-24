"""Summarize startup attempts and emulated-frame intervals; no causal stutter claim."""
import csv
import json
from pathlib import Path
import re

repo = Path(__file__).resolve().parents[2]
work = repo/'build'/'shader-profile-poc'


def distribution(values):
    ordered = sorted(values)
    if not ordered:
        return None
    return dict(frames=len(values), mean_ms=sum(values)/len(values),
                p50_ms=ordered[int((len(ordered)-1)*0.5)],
                p95_ms=ordered[int((len(ordered)-1)*0.95)],
                p99_ms=ordered[int((len(ordered)-1)*0.99)], max_ms=max(values),
                over_33_333ms=sum(x > 1000/30 for x in values),
                over_50ms=sum(x > 50 for x in values),
                over_100ms=sum(x > 100 for x in values),
                over_250ms=sum(x > 250 for x in values))


results = []
for session in json.loads((work/'matrix.json').read_text()):
    run = work/'runs'/session['run']
    text = (run/'user'/'log'/'nxemu_log.txt').read_text(errors='replace')
    summaries = list(re.finditer(r'\[\s*([\d.]+)\].*NXSHADER_POC startup attempted=(\d+) succeeded=(\d+) incompatible=(\d+) graphics_keys=(\d+) compute_keys=(\d+) elapsed_ms=([\d.]+) host_features=(.*)', text))
    if len(summaries) != 1:
        raise ValueError('expected one startup summary for '+session['run'])
    summary = summaries[0]
    startup_t = float(summary[1])
    compile_events = [(float(t), kind, key) for t, kind, key in re.findall(
        r'\[\s*([\d.]+)\].*:(CreateGraphicsPipeline|CreateComputePipeline):\d+: 0x([0-9a-f]+)', text)]
    runtime = [e for e in compile_events if e[0] > startup_t]
    csvs = list((run/'user'/'log').glob('*.csv'))
    if len(csvs) != 1:
        raise ValueError('expected one frame CSV for '+session['run'])
    with csvs[0].open() as f:
        rows = list(csv.DictReader(f))
    wall = [float(r['interval_ms']) for r in rows]
    active = [float(r['active_ms']) for r in rows]
    result = dict(session, startup=dict(log_seconds=startup_t, attempted=int(summary[2]),
                  succeeded=int(summary[3]), incompatible_graphics=int(summary[4]),
                  graphics_keys=int(summary[5]), compute_keys=int(summary[6]),
                  warmup_ms=float(summary[7]), host_features=summary[8].strip()),
                  runtime_compile_attempts=len(runtime),
                  runtime_compile_attempts_before_log_20s=sum(t < 20 for t,_,_ in runtime),
                  runtime_compile_attempts_before_log_30s=sum(t < 30 for t,_,_ in runtime),
                  runtime_graphics_attempts=sum(kind=='CreateGraphicsPipeline' for _,kind,_ in runtime),
                  runtime_compute_attempts=sum(kind=='CreateComputePipeline' for _,kind,_ in runtime),
                  unique_runtime_keys=len({(kind,key) for _,kind,key in runtime}),
                  all_compile_attempts=len(compile_events),
                  frame_intervals=distribution(wall), active_frame_durations=distribution(active),
                  first_1200_frame_intervals=distribution(wall[:1200]),
                  shader_error_lines=[line for line in text.splitlines()
                                      if '<Error>' in line and ('shader' in line.lower() or 'pipeline' in line.lower())])
    results.append(result)
(work/'measurements.json').write_text(json.dumps(dict(
    method='90s no-input title/attract; identical copied saves; five initial frames omitted; emulated frame intervals, not presented frames; one run per condition; attract route not controlled',
    driver_cache_policy='NVAPI per-executable shader disk cache OFF; NXEmu use_vulkan_driver_pipeline_cache=false; fresh process per run',
    sessions=results), indent=2))
for r in results:
    d=r['frame_intervals']
    print(r['run'], 'startup', r['startup'], 'runtime_attempts',r['runtime_compile_attempts'],
          'frames',d['frames'], 'p99_ms',round(d['p99_ms'],3), '>50ms',d['over_50ms'], 'max_ms',round(d['max_ms'],3))
