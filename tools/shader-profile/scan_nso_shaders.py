"""Check remaining shaders in decompressed base/update NSO segments, with SHA-256."""
import collections
import json
import mmap
from pathlib import Path
import re
import struct
from game_resources import resources
from nso_resources import decode_lz4
from nxshader import export_profile,sha,canonical,write_new
from rebuild_recipes import REPO,CACHE

work=REPO/'build/shader-bktr-poc'
root=Path(r'D:\Emulation\switch\NXDev\NxRoom\decrypted')
profile=json.loads((work/'resolved-profile.json').read_bytes())
_,codes,_=export_profile(CACHE,profile['title_id'],profile['content_label'])
missing={h:code for h,code in codes.items() if not profile['shaders'][h]['references']}
prefixes=collections.defaultdict(list)
for h,code in missing.items(): prefixes[code[:128]].append(h)
pattern=re.compile(b'|'.join(re.escape(x) for x in sorted({code[:16] for code in missing.values()})))
files=['Mario Kart 8 Deluxe [0100152000022000][v0].dnsp',
       'Mario Kart 8 Deluxe [0100152000022800][USA][v1245184][Update v3.0.3].dnsp']
report=[]; found=0
for relative in files:
    with (root/relative).open('rb') as f,mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ) as data:
        for entry in resources(data):
            off,size=entry['offset'],entry['size']
            if entry['kind']!='exefs' or data[off:off+4]!=b'NSO0': continue
            if size<256: raise ValueError('short NSO')
            flags,=struct.unpack_from('<I',data,off+12)
            if flags&128: raise ValueError('ZBIC not supported by this NSO locator')
            for segment in range(3):
                source,memory,decoded_size=struct.unpack_from('<III',data,off+16+segment*16)
                compressed=bool(flags&(1<<segment))
                stored=struct.unpack_from('<I',data,off+96+segment*4)[0] if compressed else decoded_size
                if source>size or stored>size-source: raise ValueError('NSO segment bounds')
                raw=data[off+source:off+source+stored]
                decoded=decode_lz4(raw,decoded_size) if compressed else raw
                expected=data[off+160+segment*32:off+192+segment*32]
                if flags&(1<<(segment+3)) and bytes.fromhex(sha(decoded))!=expected: raise ValueError('NSO segment hash mismatch')
                matches=0
                for hit in pattern.finditer(decoded):
                    pos=hit.start()
                    for h in prefixes.get(decoded[pos:pos+128],[]):
                        if profile['shaders'][h]['references'] or decoded[pos:pos+len(missing[h])]!=missing[h]: continue
                        profile['shaders'][h]['references'].append(dict(
                            file=relative,encoding='nso-lz4' if compressed else 'raw',
                            container_offset=off+source,container_size=stored,decoded_size=decoded_size,
                            segment_sha256=sha(decoded),container_sha256=sha(raw),
                            offset=pos if compressed else off+source+pos,size=len(missing[h]),
                            resource=entry['name'],segment=segment,offset_space='decoded-NSO-segment' if compressed else 'physical',
                            verified_shader_sha256=h))
                        matches+=1; found+=1
                report.append(dict(file=relative,resource=entry['name'],segment=segment,decoded_size=decoded_size,
                                   hash_verified=bool(flags&(1<<(segment+3))),new_shaders=matches))
                print(json.dumps(report[-1]),flush=True)
write_new(work/'nso-resolved-profile.json',canonical(profile))
(work/'nso-summary.json').write_text(json.dumps(dict(new_shaders=found,segments=report),indent=2))
print(json.dumps(dict(new_shaders=found,remaining=sum(not s['references'] for s in profile['shaders'].values()))))
