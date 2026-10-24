"""Verify actual known shaders through logical BKTR references, not just toy mappings."""
import collections
import json
from pathlib import Path
from bktr_resources import LogicalGames,romfs_files,resolve_references
from game_resources import decode_yaz0
from nxshader import export_profile,sha,canonical,write_new
from rebuild_recipes import REPO,CACHE

work=REPO/'build/shader-bktr-poc'; root=Path(r'D:\Emulation\switch\NXDev\NxRoom\decrypted')
profile=json.loads((work/'nso-resolved-profile.json').read_bytes())
_,codes,_=export_profile(CACHE,profile['title_id'],profile['content_label'])
by_resource=collections.defaultdict(set)
for h,shader in profile['shaders'].items():
    for ref in shader['references']:
        if '/romfs/' in ref.get('resource',''):
            by_resource[ref['resource'].split('/romfs/',1)[1].lstrip('/')].add(h)
base_file='Mario Kart 8 Deluxe [0100152000022000][v0].dnsp'
patch_file='Mario Kart 8 Deluxe [0100152000022800][USA][v1245184][Update v3.0.3].dnsp'
requests=[]; resources_checked=0; mixed_resources=0
with LogicalGames(root) as games:
    for base,patch,storage in games.pairs(base_file,patch_file):
        entries={e['name'].lstrip('/'):e for e in romfs_files(storage)}
        names=[name for name in by_resource if name in entries]
        names.sort(key=lambda name:(len({s['file'] for s in storage.spans(entries[name]['offset'],entries[name]['size'])})>1,
                                    len(by_resource[name])),reverse=True)
        for name in names:
            entry=entries[name]; raw=storage.read(entry['offset'],entry['size']); compressed=raw[:4]==b'Yaz0'
            decoded=decode_yaz0(raw,REPO/'build/shader-profile-poc/yaz0.dll') if compressed else raw
            spans=storage.spans(entry['offset'],entry['size']); resources_checked+=1
            mixed_resources+=len({s['file'] for s in spans})>1
            for h in by_resource[name]:
                offset=decoded.find(codes[h])
                if offset<0:continue
                ref=dict(file=patch_file,base_file=base_file,base_nca=base['name'],patch_nca=patch['name'],
                         section=patch['index'],resource=name,resource_offset=entry['offset'],
                         resource_size=entry['size'],resource_sha256=sha(raw),
                         encoding='bktr-yaz0' if compressed else 'bktr-raw',offset=offset,size=len(codes[h]),
                         offset_space='decoded-logical-resource' if compressed else 'logical-resource',
                         verified_shader_sha256=h,physical_spans=spans)
                requests.append((h,ref))
                if len(requests)>=20:break
            if len(requests)>=20:break
decoded=resolve_references(profile,root,requests,REPO/'build/shader-profile-poc/yaz0.dll')
assert len(decoded)==len(requests) and all(decoded[h]==codes[h] for h,_ in requests)
for h,ref in requests: profile['shaders'][h]['references'].insert(0,ref)
write_new(work/'logical-verified-profile-controls.json',canonical(profile))
report=dict(actual_shader_locators_verified=len(requests),resources_checked=resources_checked,
            mixed_base_and_patch_resources=mixed_resources,
            references=[dict(shader=h,reference=ref) for h,ref in requests])
(work/'bktr-locator-verification.json').write_text(json.dumps(report,indent=2))
print(json.dumps({k:v for k,v in report.items() if k!='references'},indent=2))
