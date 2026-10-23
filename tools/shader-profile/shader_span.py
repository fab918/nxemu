"""Normalize only a proven extra cache word, never change shader instructions."""
import struct
from nxshader import sha,canonical

TERMINAL_BRANCHES={0xE2400FFFFF87000F,0xE2400FFFFF07000F}


def without_extra_cache_word(code):
    if len(code)<96 or len(code)%8 or struct.unpack_from('<Q',code,len(code)-16)[0] not in TERMINAL_BRANCHES:
        raise ValueError('extra cache word after terminal branch is not established')
    return code[:-8]


def rebind_resource_span(profile,old,code,reference):
    candidate=without_extra_cache_word(code)
    digest=sha(candidate)
    if reference['size']!=len(candidate):raise ValueError('resource span size mismatch')
    profile['shaders'][digest]=dict(size=len(candidate),references=[dict(reference,verified_shader_sha256=digest)])
    remap={}
    for eid,entry in list(profile['environments'].items()):
        if entry['shader']!=old:continue
        if entry['guest_read_highest']-entry['guest_read_lowest']+8!=len(code):raise ValueError('cache bounds mismatch')
        new=dict(entry,shader=digest,guest_read_highest=entry['guest_read_highest']-8)
        nid=sha(canonical(new));profile['environments'][nid]=new;remap[eid]=nid
    for p in profile['pipelines']:p['environments']=[remap.get(e,e) for e in p['environments']]
    for eid in remap:del profile['environments'][eid]
    del profile['shaders'][old]
    return digest
