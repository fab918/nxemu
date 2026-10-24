"""Find complete physical Yaz0 streams, including inside an unmapped patch section."""
import collections
import ctypes as c
import json
import mmap
from pathlib import Path
import re
import struct
import time
from nxshader import canonical, export_profile, resolve_code, sha, write_new
from rebuild_recipes import CACHE, REPO, WORK

root=Path(r'D:\Emulation\switch\NXDev\NxRoom\decrypted')
relative='Mario Kart 8 Deluxe [0100152000022800][USA][v1245184][Update v3.0.3].dnsp'
profile=json.loads((WORK/'resolved-profile.json').read_bytes())
_,codes,_=export_profile(CACHE,profile['title_id'],profile['content_label'])
missing={h:code for h,code in codes.items() if not profile['shaders'][h]['references']}
prefixes=collections.defaultdict(list)
for h,code in missing.items(): prefixes[code[:128]].append(h)
pattern=re.compile(b'|'.join(re.escape(x) for x in sorted({code[:16] for code in missing.values()})))
dll=c.CDLL(str(REPO/'build/shader-recipe-poc/tests/Release/yaz0-scan.dll'))
decode=dll.nxshader_yaz0_extent
decode.argtypes=[c.c_void_p,c.c_size_t,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)]
decode.restype=c.c_int
started=time.monotonic(); count=valid=invalid=matches=decoded_bytes=0
with (root/relative).open('rb') as file,mmap.mmap(file.fileno(),0,access=mmap.ACCESS_READ) as data:
    pos=0
    while True:
        pos=data.find(b'Yaz0',pos)
        if pos<0: break
        start=pos; pos+=4
        if start+16>len(data): continue
        expected,=struct.unpack_from('>I',data,start+4)
        if not 0<expected<=256*1024*1024 or data[start+8:start+16]!=bytes(8): continue
        count+=1
        bound=min(len(data)-start,16+expected+(expected+7)//8)
        raw=data[start:start+bound]
        output=c.create_string_buffer(expected); consumed=c.c_size_t()
        status=decode(raw,len(raw),output,expected,c.byref(consumed))
        if status:
            invalid+=1; continue
        valid+=1; decoded=output.raw; decoded_bytes+=len(decoded)
        compressed_sha=None
        for hit in pattern.finditer(decoded):
            off=hit.start()
            for h in prefixes.get(decoded[off:off+128],[]):
                if profile['shaders'][h]['references'] or decoded[off:off+len(missing[h])]!=missing[h]: continue
                if compressed_sha is None: compressed_sha=sha(raw[:consumed.value])
                profile['shaders'][h]['references'].append(dict(
                    file=relative,encoding='yaz0',container_offset=start,container_size=consumed.value,
                    container_sha256=compressed_sha,offset=off,size=len(missing[h]),
                    resource='physical-Yaz0-stream; logical BKTR path unresolved',
                    offset_space='decoded-resource',verified_shader_sha256=h))
                matches+=1
        if valid%40==0: print(json.dumps(dict(valid=valid,matches=matches,decoded_bytes=decoded_bytes)),flush=True)
resolved=resolve_code(profile,root,REPO/'build/shader-profile-poc/yaz0.dll')
assert all(code==codes[h] for h,code in resolved.items())
write_new(WORK/'physical-resolved-profile.json',canonical(profile))
sample=json.loads((WORK/'preparation.json').read_bytes())['selected']
summary=dict(candidates=count,complete_streams=valid,failed_streams=invalid,new_exact_shaders=matches,
             decoded_bytes=decoded_bytes,seconds=time.monotonic()-started,
             total_shaders=len(codes),resolved_shaders=len(resolved),
             sample_reconstructible=sum(all(profile['environments'][e]['shader'] in resolved
                                            for e in case['pipeline']['environments']) for case in sample),
             sample_total=len(sample),
             full_cache_reconstructible=sum(all(profile['environments'][e]['shader'] in resolved
                                                for e in p['environments']) for p in profile['pipelines']),
             full_cache_total=len(profile['pipelines']),
             unresolved_shaders=[h for h in codes if h not in resolved])
(WORK/'physical-resource-summary.json').write_text(json.dumps(summary,indent=2))
print(json.dumps({k:v for k,v in summary.items() if k!='unresolved_shaders'},indent=2))
