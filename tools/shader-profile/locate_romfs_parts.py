"""Check natural program-header/instruction-body boundaries in logical RomFS assets."""
import collections
import json
from pathlib import Path
import re
from bktr_resources import LogicalGames,romfs_files
from game_resources import decode_yaz0
from nxshader import export_profile,sha
from rebuild_recipes import REPO,CACHE

work=REPO/'build/shader-bktr-poc'; root=Path(r'D:\Emulation\switch\NXDev\NxRoom\decrypted')
profile=json.loads((work/'nso-resolved-profile.json').read_bytes())
_,codes,_=export_profile(CACHE,profile['title_id'],profile['content_label'])
missing={h:code for h,code in codes.items() if not profile['shaders'][h]['references']}
anchors=collections.defaultdict(list)
for h,code in missing.items():
    anchors[code[:80]].append((h,'header',code[:80]))
    anchors[code[80:144]].append((h,'body',code[80:]))
pattern=re.compile(b'|'.join(re.escape(a) for a in sorted(anchors,key=len,reverse=True)))
matches={h:dict(header=[],body=[]) for h in missing}
base_file='Mario Kart 8 Deluxe [0100152000022000][v0].dnsp'
patch_file='Mario Kart 8 Deluxe [0100152000022800][USA][v1245184][Update v3.0.3].dnsp'
with LogicalGames(root) as games:
    for base,patch,storage in games.pairs(base_file,patch_file):
        for i,entry in enumerate(romfs_files(storage)):
            if entry['size']>512*1024*1024: raise ValueError('oversize resource in diagnostic')
            raw=storage.read(entry['offset'],entry['size']); compressed=raw[:4]==b'Yaz0'
            decoded=decode_yaz0(raw,REPO/'build/shader-profile-poc/yaz0.dll') if compressed else raw
            for hit in pattern.finditer(decoded):
                pos=hit.start()
                candidates=anchors.get(decoded[pos:pos+80],[])+anchors.get(decoded[pos:pos+64],[])
                for h,label,needle in candidates:
                    if matches[h][label] or decoded[pos:pos+len(needle)]!=needle:continue
                    matches[h][label].append(dict(file=patch_file,base_file=base_file,
                        base_nca=base['name'],patch_nca=patch['name'],section=patch['index'],
                        resource=entry['name'],resource_offset=entry['offset'],resource_size=entry['size'],
                        resource_sha256=sha(raw),encoding='bktr-yaz0' if compressed else 'bktr-raw',
                        offset=pos,size=len(needle)))
            if i%500==0: print(json.dumps(dict(files=i,body_matches=sum(bool(x['body']) for x in matches.values()),
                                              header_matches=sum(bool(x['header']) for x in matches.values()))),flush=True)
(work/'romfs-parts.json').write_text(json.dumps(matches,indent=2))
print(json.dumps(dict(total=len(matches),body_matches=sum(bool(x['body']) for x in matches.values()),
                      header_matches=sum(bool(x['header']) for x in matches.values()),
                      both=sum(bool(x['body'] and x['header']) for x in matches.values())),indent=2))
