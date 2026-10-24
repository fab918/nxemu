"""Diagnose natural header/body boundaries; do not synthesize instructions."""
import json
import mmap
from pathlib import Path
import struct
from game_resources import resources
from nso_resources import decode_lz4
from nxshader import export_profile,sha
from rebuild_recipes import REPO,CACHE

work=REPO/'build/shader-bktr-poc'; root=Path(r'D:\Emulation\switch\NXDev\NxRoom\decrypted')
profile=json.loads((work/'nso-resolved-profile.json').read_bytes())
_,codes,_=export_profile(CACHE,profile['title_id'],profile['content_label'])
missing={h:c for h,c in codes.items() if not profile['shaders'][h]['references']}
parts={h:dict(header=[],body=[]) for h in missing}
for relative in ['Mario Kart 8 Deluxe [0100152000022000][v0].dnsp',
                 'Mario Kart 8 Deluxe [0100152000022800][USA][v1245184][Update v3.0.3].dnsp']:
    with (root/relative).open('rb') as f,mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ) as data:
        for entry in resources(data):
            off=entry['offset']
            if entry['kind']!='exefs' or data[off:off+4]!=b'NSO0':continue
            flags,=struct.unpack_from('<I',data,off+12)
            for segment in range(3):
                start,mem,size=struct.unpack_from('<III',data,off+16+segment*16)
                compressed=bool(flags&(1<<segment))
                stored=struct.unpack_from('<I',data,off+96+segment*4)[0] if compressed else size
                raw=data[off+start:off+start+stored]; decoded=decode_lz4(raw,size) if compressed else raw
                for h,code in missing.items():
                    for label,needle in [('header',code[:80]),('body',code[80:])]:
                        position=decoded.find(needle)
                        if position<0: continue
                        parts[h][label].append(dict(file=relative,resource=entry['name'],segment=segment,
                            container_offset=off+start,container_size=stored,decoded_size=size,
                            container_sha256=sha(raw),segment_sha256=sha(decoded),encoding='nso-lz4' if compressed else 'raw',
                            offset=position if compressed else off+start+position,size=len(needle)))
(work/'nso-parts.json').write_text(json.dumps(parts,indent=2))
print(json.dumps(dict(missing=len(missing),complete_bodies=sum(bool(p['body']) for p in parts.values()),
                      complete_headers=sum(bool(p['header']) for p in parts.values()),
                      both=sum(bool(p['header'] and p['body']) for p in parts.values())),indent=2))
