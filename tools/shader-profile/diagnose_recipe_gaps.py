"""Explain unjoined guest observations without inventing missing shader code."""
import json
from pathlib import Path
import struct
from rebuild_recipes import WORK, RECIPES, normalized, effective_size

prep=json.loads((WORK/'preparation.json').read_bytes())
capture=json.loads(RECIPES.read_bytes()); n=capture['guest_state_word_count']
projections=(WORK/'projection/projection-output.bin').read_bytes()
deps={int(k):v for k,v in prep['dependencies'].items()}
indexed={}
for i,p in enumerate(prep['profile']['pipelines']):
    if p['kind']!='graphics': continue
    key=normalized(bytes.fromhex(p['key_le_hex']),deps)
    indexed[key[:416+effective_size(key)]]=(i,p)
out=[]
for gap in prep['failures']:
    rid=gap['recipe']; w=capture['recipes'][rid]['words']
    header=struct.pack('<12I',*w[n:n+12])+struct.pack('<90I',*w[n+12:n+102])+struct.pack('<2I',*w[n+102:])
    offset=rid*988
    size,=struct.unpack_from('<I',projections,offset)
    key=normalized(header+projections[offset+4:offset+988],deps)[:416+size]
    item=dict(gap,shader_hashes=[w[i]|w[i+1]<<32 for i in range(n,n+12,2)])
    twin=bytearray(key)
    raw2,=struct.unpack_from('<I',twin,420)
    hint=raw2>>17&7
    struct.pack_into('<I',twin,420,(raw2&~(7<<17)) | ((1-hint)<<17) if hint in (0,1) else raw2)
    found=indexed.get(bytes(twin)) if hint in (0,1) else None
    if found:
        index,p=found
        no_replacements=all(not prep['profile']['environments'][e]['cbuf_replacements'] for e in p['environments'])
        item.update(classification='HLE/no-HLE twin omitted from serialized cache' if no_replacements else 'unproven HLE alias',
                    serialized_twin_pipeline=index,no_cbuf_replacements=no_replacements,
                    differing_bits='app_stage only')
    else:
        nearest=[]
        for k,(index,p) in indexed.items():
            if k[:48]==key[:48]:
                differences=[dict(offset=i,recipe=a,cache=b) for i,(a,b) in enumerate(zip(key,k)) if a!=b]
                nearest.append((len(differences),index,differences))
        item.update(classification='no exact cache record',nearest_same_shaders=sorted(nearest)[:3])
    out.append(item)
(WORK/'recipe-gaps.json').write_text(json.dumps(out,indent=2))
print(json.dumps(out,indent=2))
