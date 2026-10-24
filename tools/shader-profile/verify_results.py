"""Offline evidence collection after timing runs; source is used only for comparison."""
import collections
import json
from pathlib import Path
import struct
from nxshader import load_profile, parse_cache, read_cache, sha

repo = Path(__file__).resolve().parents[2]
work = repo/'build'/'shader-profile-poc'
data = read_cache(r'C:\Users\fabri\Downloads\MarioKartMacvulkan.bin.zip')
records = parse_cache(data)
profile = load_profile(work/'mariokart-resolved.nxshader')


def effective_key(key, compute):
    if compute:
        return key
    raw1, raw2 = struct.unpack_from('<II', key, 416)
    if raw1 & (1 << 6):
        state_size = 984
    elif raw1 & (1 << 5) and raw1 & (1 << 3):
        state_size = 64
    elif raw1 & (1 << 5):
        state_size = 104
    elif raw1 & 1 and not raw2 & (1 << 20):
        state_size = 360
    else:
        state_size = 424
    return key[:416+state_size]


raw_keys = collections.Counter((c,k) for _,k,c,_ in records)
effective = collections.Counter((c,effective_key(k,c)) for _,k,c,_ in records)
source_blobs = {sha(code) for envs,_,_,_ in records for _,code in envs}
source_combinations = {tuple(sha(code) for _,code in envs) for envs,_,_,_ in records}
source_blob_bytes = sum(len(code) for envs,_,_,_ in records for _,code in envs)
unique = {sha(code):code for envs,_,_,_ in records for _,code in envs}
encoded_profile = json.dumps(profile).encode()
report = dict(source_records=len(records), full_key_duplicates=sum(v-1 for v in raw_keys.values()),
              effective_key_duplicates=sum(v-1 for v in effective.values()),
              code_occurrences=sum(len(envs) for envs,_,_,_ in records),
              serialized_code_bytes=source_blob_bytes, unique_code_bytes=sum(map(len,unique.values())),
              redundant_code_bytes=source_blob_bytes-sum(map(len,unique.values())),
              resolved_shaders=sum(bool(s['references']) for s in profile['shaders'].values()),
              unresolved_shaders=sum(not s['references'] for s in profile['shaders'].values()),
              manifest_bytes=(work/'mariokart-resolved.nxshader').stat().st_size,
              no_shader_hex_in_manifest=all(c.hex().encode() not in encoded_profile for c in unique.values()),
              observed_sessions=[])
for label in ('A-empty','B-macos','C-manifest','D-local','E-local-v20'):
    path = work/'runs'/label/'user'/'shader'/'0100152000022000'/'vulkan.bin'
    local = parse_cache(path.read_bytes())
    # Counts include imported records. A is the clean observation of newly
    # requested serializable runtime pipelines; others are not directly coverage samples.
    combinations = [tuple(sha(code) for _,code in envs) for envs,_,_,_ in local]
    report['observed_sessions'].append(dict(run=label, saved_records=len(local),
        shader_combination_present_in_source=sum(c in source_combinations for c in combinations),
        all_shader_blobs_present_in_source=sum(all(h in source_blobs for h in c) for c in combinations)))
(work/'additional-evidence.json').write_text(json.dumps(report, indent=2))
print(json.dumps(report, indent=2))
