"""Exact shader discovery in the logical, patched RomFS; metadata-only output."""
import collections
import json
from pathlib import Path
import re
import time
from bktr_resources import LogicalGames,romfs_files
from game_resources import decode_yaz0
from nxshader import export_profile,sha,canonical,write_new,resolve_code
from rebuild_recipes import REPO,CACHE,WORK as OLD_WORK

WORK=REPO/'build/shader-bktr-poc'
root=Path(r'D:\Emulation\switch\NXDev\NxRoom\decrypted')
base_file='Mario Kart 8 Deluxe [0100152000022000][v0].dnsp'
patch_file='Mario Kart 8 Deluxe [0100152000022800][USA][v1245184][Update v3.0.3].dnsp'
profile=json.loads((OLD_WORK/'physical-resolved-profile.json').read_bytes())
_,codes,_=export_profile(CACHE,profile['title_id'],profile['content_label'])
missing={h:code for h,code in codes.items() if not profile['shaders'][h]['references']}
prefixes=collections.defaultdict(list)
for h,code in missing.items(): prefixes[code[:128]].append(h)
pattern=re.compile(b'|'.join(re.escape(x) for x in sorted({code[:16] for code in missing.values()})))
started=time.monotonic(); files=decoded_bytes=found=0; skipped=[]; mapping=[]
with LogicalGames(root) as games:
    for base,patch,storage in games.pairs(base_file,patch_file):
        entries=list(romfs_files(storage))
        mapping.append(dict(base_nca=base['name'],patch_nca=patch['name'],section=patch['index'],
                            logical_romfs_size=storage.size,files=len(entries),
                            bktr_entries=len(storage.source.entries)))
        print(json.dumps(mapping[-1]),flush=True)
        write_new(WORK/'logical-romfs-index.json',canonical(entries))
        for entry in entries:
            files+=1
            if entry['size']>512*1024*1024:
                skipped.append(entry); continue
            raw=storage.read(entry['offset'],entry['size'])
            compressed=raw[:4]==b'Yaz0'
            decoded=decode_yaz0(raw,REPO/'build/shader-profile-poc/yaz0.dll') if compressed else raw
            decoded_bytes+=len(decoded)
            for hit in pattern.finditer(decoded):
                offset=hit.start()
                for h in prefixes.get(decoded[offset:offset+128],[]):
                    if profile['shaders'][h]['references'] or decoded[offset:offset+len(missing[h])]!=missing[h]: continue
                    ref=dict(file=patch_file,base_file=base_file,base_nca=base['name'],patch_nca=patch['name'],
                             section=patch['index'],resource=entry['name'],resource_offset=entry['offset'],
                             resource_size=entry['size'],resource_sha256=sha(raw),
                             encoding='bktr-yaz0' if compressed else 'bktr-raw',
                             offset=offset,size=len(missing[h]),offset_space='decoded-logical-resource' if compressed else 'logical-resource',
                             verified_shader_sha256=h,physical_spans=storage.spans(entry['offset'],entry['size']))
                    profile['shaders'][h]['references'].append(ref); found+=1
            if files%100==0: print(json.dumps(dict(files=files,decoded_bytes=decoded_bytes,new_shaders=found)),flush=True)
resolved=resolve_code(profile,root,REPO/'build/shader-profile-poc/yaz0.dll')
assert all(code==codes[h] for h,code in resolved.items())
write_new(WORK/'resolved-profile.json',canonical(profile))
summary=dict(indexed_files=files,decoded_bytes=decoded_bytes,mappings=mapping,skipped_oversize=skipped,
             new_exact_shaders=found,resolved_shaders=len(resolved),total_shaders=len(codes),
             unresolved_shaders=[h for h in codes if h not in resolved],seconds=time.monotonic()-started)
(WORK/'bktr-summary.json').write_text(json.dumps(summary,indent=2))
print(json.dumps(summary,indent=2))
