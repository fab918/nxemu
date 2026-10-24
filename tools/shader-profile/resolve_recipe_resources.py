"""P1 exact resource matching for the new capture; writes no shader code in profiles."""
import collections
import copy
import json
from pathlib import Path
from nxshader import canonical, export_profile, scan_game, resolve_code, sha, write_new
from scan_compressed import scan
from rebuild_recipes import CACHE, REPO, WORK

root=Path(r'D:\Emulation\switch\NXDev\NxRoom\decrypted')
files=['Mario Kart 8 Deluxe [0100152000022000][v0].dnsp',
       'Mario Kart 8 Deluxe [0100152000022800][USA][v1245184][Update v3.0.3].dnsp']
preparation=json.loads((WORK/'preparation.json').read_bytes())
profile=preparation['profile']
_,codes,_=export_profile(CACHE,profile['title_id'],profile['content_label'])
missing={h:code for h,code in codes.items() if not profile['shaders'][h]['references']}
raw=scan_game(profile,missing,root,files) if missing else {}
remaining={h:c for h,c in codes.items() if not profile['shaders'][h]['references']}
compressed=scan(profile,remaining,root,files,REPO/'build/shader-profile-poc/yaz0.dll') if remaining else []
for digest,shader in profile['shaders'].items():
    for ref in shader['references']:
        ref['verified_shader_sha256']=digest
resolved=resolve_code(profile,root,REPO/'build/shader-profile-poc/yaz0.dll')
assert all(sha(c)==h and c==codes[h] for h,c in resolved.items())
unresolved=[h for h in codes if h not in resolved]
selected=preparation['selected']
sample_success=[]; sample_fail=[]
for c in selected:
    missing=[profile['environments'][e]['shader'] for e in c['pipeline']['environments']
             if profile['environments'][e]['shader'] not in resolved]
    (sample_fail if missing else sample_success).append(dict(recipe_id=c['recipe_id'],missing_shaders=missing))
write_new(WORK/'resolved-profile.json',canonical(profile))
summary=dict(total_shaders=len(codes),resolved_shaders=len(resolved),unresolved_shaders=unresolved,
             encoding_counts=dict(collections.Counter(r['encoding'] for s in profile['shaders'].values()
                                                       for r in s['references'])),
             sample_size=len(selected),sample_reconstructible=len(sample_success),sample_failures=sample_fail,
             full_cache_reconstructible=sum(all(profile['environments'][e]['shader'] in resolved
                                                 for e in p['environments']) for p in profile['pipelines']),
             full_cache_pipelines=len(profile['pipelines']),raw_scan=raw,compressed_scan=compressed)
(WORK/'resource-summary.json').write_text(json.dumps(summary,indent=2))
print(json.dumps(summary,indent=2))
