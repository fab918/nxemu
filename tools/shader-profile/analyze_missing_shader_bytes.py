"""Classify unresolved cached blobs conservatively, with no bytes in the report."""
import collections
import json
from nxshader import load_profile, parse_cache, read_cache, sha
from rebuild_recipes import CACHE, REPO, WORK

profile=json.loads((WORK/'physical-resolved-profile.json').read_bytes())
current=parse_cache(read_cache(CACHE))
current_codes={sha(code):(meta,code) for envs,key,compute,bounds in current for meta,code in envs}
mac=load_profile(REPO/'build/shader-profile-poc/MarioKart-cache-macOS.nxshader')
mac_records=parse_cache(read_cache(r'C:\Users\fabri\Downloads\MarioKartMacvulkan.bin.zip'))
known={}
for h,(meta,code) in current_codes.items():
    if profile['shaders'][h]['references']: known[h]=(meta,code)
for envs,key,compute,bounds in mac_records:
    for meta,code in envs:
        h=sha(code)
        if mac['shaders'][h]['references']: known[h]=(meta,code)
bodies=collections.defaultdict(list); prefixes=collections.defaultdict(list)
for h,(meta,code) in known.items():
    if meta['stage']==5: continue
    bodies[sha(code[80:])].append(h)
    prefixes[code[80:144]].append(h)
summary=[]
for h,(meta,code) in current_codes.items():
    if profile['shaders'][h]['references']: continue
    body=bodies.get(sha(code[80:]),[]) if meta['stage']!=5 else []
    prefix=prefixes.get(code[80:144],[]) if meta['stage']!=5 else []
    item=dict(shader_sha256=h,stage=meta['stage'],size=len(code),
              classification='unlocated; dynamic origin not established',
              exact_body_matches_known_resources=body,body_prefix_candidates=len(prefix))
    if body:
        buddy=known[body[0]][1]
        item.update(classification='same instruction-body bytes, different 80-byte program header',
                    differing_header_byte_offsets=[i for i,(a,b) in enumerate(zip(code[:80],buddy[:80])) if a!=b])
    summary.append(item)
(WORK/'missing-shader-diagnosis.json').write_text(json.dumps(summary,indent=2))
print(json.dumps(dict(total_unlocated=len(summary),classifications=dict(collections.Counter(x['classification'] for x in summary))),indent=2))
