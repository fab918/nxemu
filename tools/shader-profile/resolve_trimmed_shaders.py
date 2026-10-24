"""Diagnose the extra cached word after the recognized shader termination branch.

Only resource-backed bytes are emitted. No instruction or guessed padding is added.
"""
import copy
import json
from pathlib import Path
import struct
from bktr_resources import LogicalGames
from game_resources import decode_yaz0
from nxshader import export_profile,sha,canonical,write_new,write_profile
from rebuild_recipes import REPO,CACHE

work=REPO/'build/shader-origin-poc'
previous=REPO/'build/shader-bktr-poc'
profile=json.loads((previous/'full-profile-logical.json').read_bytes())
parts=json.loads((previous/'romfs-parts.json').read_bytes())
_,codes,_=export_profile(CACHE,profile['title_id'],profile['content_label'])
missing={h:code for h,code in codes.items() if not profile['shaders'][h]['references']}
requests={}
for match in parts.values():
    for ref in match['header']:
        identity=tuple(ref[k] for k in ('base_file','file','base_nca','patch_nca','section','resource_offset','resource_size'))
        requests[identity]=ref
root=Path(r'D:\Emulation\switch\NXDev\NxRoom\decrypted')
found={}
with LogicalGames(root) as games:
    for identity,resource_ref in requests.items():
        base_file,patch_file,base_nca,patch_nca,index,offset,size=identity
        storage=games.patched(base_file,patch_file,base_nca,patch_nca,index)
        raw=storage.read(offset,size)
        decoded=decode_yaz0(raw,REPO/'build/shader-profile-poc/yaz0.dll') if resource_ref['encoding']=='bktr-yaz0' else raw
        for old,code in missing.items():
            if old in found:continue
            if struct.unpack_from('<Q',code,len(code)-16)[0] not in (0xE2400FFFFF87000F,0xE2400FFFFF07000F):
                raise ValueError('no terminal branch immediately before extra cache word')
            candidate=code[:-8]; pos=decoded.find(candidate)
            if pos<0:continue
            h=sha(candidate)
            ref=dict(resource_ref,offset=pos,size=len(candidate),verified_shader_sha256=h,
                     offset_space='decoded-logical-resource',physical_spans=storage.spans(offset,size))
            found[old]=dict(shader=h,reference=ref,size=len(candidate),removed_bytes=8,
                            original_size=len(code),terminal_branch_preserved=True)
            profile['shaders'][h]=dict(size=len(candidate),references=[ref])

remap={}
for old,entry in list(profile['environments'].items()):
    if entry['shader'] not in found:continue
    replacement=found[entry['shader']]
    e=dict(entry,shader=replacement['shader'],guest_read_highest=entry['guest_read_highest']-8)
    new=sha(canonical(e)); profile['environments'][new]=e; remap[old]=new
for p in profile['pipelines']:p['environments']=[remap.get(e,e) for e in p['environments']]
for old in remap:del profile['environments'][old]
for old in found:del profile['shaders'][old]
write_new(work/'resolved-profile.json',canonical(profile))
sizes=write_profile(work/'MarioKart-complete.nxshader',profile)
summary=dict(targets=len(missing),resources_checked=len(requests),located_trimmed=len(found),
             remaining=[h for h in missing if h not in found],changes=found,
             manifest_bytes=sizes[1],json_bytes=sizes[0],
             manifest_sha256=sha((work/'MarioKart-complete.nxshader').read_bytes()))
(work/'trimmed-discovery.json').write_text(json.dumps(summary,indent=2))
print(json.dumps({k:v for k,v in summary.items() if k!='changes'},indent=2))
