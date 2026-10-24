"""Summarize numeric guest recipe diagnostics without copying any shader code."""
import gzip
import hashlib
import json
from pathlib import Path
import sys

source = Path(sys.argv[1])
raw = source.read_bytes()
report = json.loads(raw)
assert report['schema'] == 'nxshader-guest-state-probe-1'
assert report['shader_instructions_included'] is False
n = report['guest_state_word_count']
recipes = report['recipes']
assert sum(r['observations'] for r in recipes) + report['dropped_observations'] == report['observations']
for r in recipes:
    w = r['words']
    assert len(w) == n + 12 + 90 + 2
    assert w[0] == 1 and w[1] & 63 == 0
    assert all(type(x) is int and 0 <= x <= 0xffffffff for x in w)
shader_sets = {tuple(r['words'][n:n+12]) for r in recipes}
shader_ids = {tuple(r['words'][i:i+2]) for r in recipes for i in range(n,n+12,2)
              if any(r['words'][i:i+2])}
compressed = gzip.compress(raw, mtime=0)
destination = source.with_suffix('.nxshader-probe.json.gz')
destination.write_bytes(compressed)
summary = {k:v for k,v in report.items() if k != 'recipes'}
summary.update(unique_recipes=len(recipes), unique_shader_stage_sets=len(shader_sets),
               unique_shader_hashes=len(shader_ids), bytes_json=len(raw), bytes_gzip=len(compressed),
               sha256_gzip=hashlib.sha256(compressed).hexdigest(),
               probe_us_per_observation=report['probe_cpu_ns']/max(report['observations'],1)/1000,
               scope='fixed graphics state only; not a complete shareable compilation environment')
source.with_name('summary.json').write_text(json.dumps(summary, indent=2))
print(json.dumps(summary, indent=2))
