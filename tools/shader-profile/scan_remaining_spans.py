"""Find complete resource-backed program spans, excluding only the extra cache word."""
import collections
import json
import mmap
from pathlib import Path
import re
import struct
from bktr_resources import LogicalGames,romfs_files
from game_resources import decode_yaz0,resources
from nso_resources import decode_lz4
from shader_span import without_extra_cache_word,rebind_resource_span
from nxshader import export_profile,sha,canonical,write_new,write_profile
from rebuild_recipes import REPO,CACHE

work=REPO/'build/shader-origin-poc'; root=Path(r'D:\Emulation\switch\NXDev\NxRoom\decrypted')
profile=json.loads((work/'resolved-profile.json').read_bytes())
_,codes,_=export_profile(CACHE,profile['title_id'],profile['content_label'])
missing={h:code for h,code in codes.items() if h in profile['shaders'] and not profile['shaders'][h]['references']}
targets={h:without_extra_cache_word(code) for h,code in missing.items()}
prefixes=collections.defaultdict(list)
for h,code in targets.items():prefixes[code[:128]].append(h)
pattern=re.compile(b'|'.join(re.escape(x) for x in sorted({c[:16] for c in targets.values()})))
found={}
def search(decoded,ref):
    for hit in pattern.finditer(decoded):
        pos=hit.start()
        for old in prefixes.get(decoded[pos:pos+128],[]):
            if old in found or decoded[pos:pos+len(targets[old])]!=targets[old]:continue
            locator=dict(ref,offset=pos,size=len(targets[old]))
            if ref['encoding']=='raw':locator['offset']=ref['physical_base']+pos;del locator['physical_base']
            digest=rebind_resource_span(profile,old,missing[old],locator)
            found[old]=dict(shader=digest,reference=locator,removed_bytes=8,terminal_branch_preserved=True)
base_file='Mario Kart 8 Deluxe [0100152000022000][v0].dnsp'
patch_file='Mario Kart 8 Deluxe [0100152000022800][USA][v1245184][Update v3.0.3].dnsp'
for relative in (base_file,patch_file):
    with (root/relative).open('rb') as f,mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ) as data:
        for entry in resources(data):
            off=entry['offset']
            if entry['kind']!='exefs' or data[off:off+4]!=b'NSO0':continue
            flags,=struct.unpack_from('<I',data,off+12)
            if flags&128:raise ValueError('unsupported NSO codec')
            for segment in range(3):
                start,mem,size=struct.unpack_from('<III',data,off+16+segment*16)
                compressed=bool(flags&(1<<segment))
                stored=struct.unpack_from('<I',data,off+96+segment*4)[0] if compressed else size
                raw=data[off+start:off+start+stored];decoded=decode_lz4(raw,size) if compressed else raw
                expected=data[off+160+segment*32:off+192+segment*32]
                if flags&(1<<(segment+3)) and bytes.fromhex(sha(decoded))!=expected:raise ValueError('NSO hash mismatch')
                ref=dict(file=relative,resource=entry['name'],encoding='nso-lz4' if compressed else 'raw',
                         container_offset=off+start,container_size=stored,decoded_size=size,
                         segment_sha256=sha(decoded),container_sha256=sha(raw),segment=segment,
                         offset_space='decoded-NSO-segment' if compressed else 'physical')
                if not compressed:ref['physical_base']=off+start
                search(decoded,ref)
with LogicalGames(root) as games:
    for base,patch,storage in games.pairs(base_file,patch_file):
        for i,entry in enumerate(romfs_files(storage)):
            if len(found)==len(targets):break
            raw=storage.read(entry['offset'],entry['size']);compressed=raw[:4]==b'Yaz0'
            decoded=decode_yaz0(raw,REPO/'build/shader-profile-poc/yaz0.dll') if compressed else raw
            ref=dict(file=patch_file,base_file=base_file,base_nca=base['name'],patch_nca=patch['name'],
                     section=patch['index'],resource=entry['name'],resource_offset=entry['offset'],resource_size=entry['size'],
                     resource_sha256=sha(raw),encoding='bktr-yaz0' if compressed else 'bktr-raw',
                     offset_space='decoded-logical-resource',physical_spans=storage.spans(entry['offset'],entry['size']))
            search(decoded,ref)
            if i%300==0:print(json.dumps(dict(files=i,located=len(found),targets=len(targets))),flush=True)
write_new(work/'complete-profile.json',canonical(profile))
sizes=write_profile(work/'MarioKart-resource-spans.nxshader',profile)
summary=dict(targets=len(targets),found=len(found),remaining=[h for h in targets if h not in found],
             changes=found,manifest_bytes=sizes[1],json_bytes=sizes[0],
             manifest_sha256=sha((work/'MarioKart-resource-spans.nxshader').read_bytes()))
(work/'remaining-discovery.json').write_text(json.dumps(summary,indent=2))
print(json.dumps({k:v for k,v in summary.items() if k!='changes'},indent=2))
