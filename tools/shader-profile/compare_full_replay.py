"""Full-corpus comparison, with graphics alias rules and separate compute evidence."""
import collections
import json
from pathlib import Path
import re
import struct
from compare_rebuild_trace import trace,compare,digest
from full_recipe_manifest import WORK
from rebuild_recipes import REPO,CAPTURE


def normalize_compute_key(original,mask):
    if len(original)!=64: raise ValueError('bad compute key size')
    key=bytearray(original)
    for bank in range(8):
        if not mask&(1<<bank): struct.pack_into('<I',key,24+bank*4,0)
    return bytes(key)


def compute_trace(folder):
    events=collections.defaultdict(lambda:collections.defaultdict(list))
    for path in (folder/'trace').glob('*.bin'):
        match=re.fullmatch(r'([0-9a-f]{16})-(compute-key-live|compute-key-disk|compute-spirv|compute-native-success|ir)-(\d+)-(\d+)\.bin',path.name)
        if not match: continue
        h,kind,stage,seq=match.groups()
        if stage!='6': continue
        data=path.read_bytes()
        if kind=='ir': data=re.sub(rb'(?m)^\[[0-9a-fA-F]{16}\] ',b'',data)
        events[h][kind].append((data,str(path.relative_to(folder))))
    result={}
    for h,entry in events.items():
        keys={data for data,path in entry.get('compute-key-disk',[])}
        if not keys: continue
        if len(keys)!=1: raise ValueError('ambiguous compute hash')
        key=keys.pop()
        if len(key)!=64: raise ValueError('bad compute key size')
        result[key]=entry
    return result


def run(case,resolved_only=False,profile_path=None):
    profile=json.loads((WORK/'full-profile.json').read_bytes())
    selected=json.loads((WORK/'full-selection.json').read_bytes())
    deps={int(k):v for k,v in profile['guest_recipe_probe']['dependency_masks'].items()}
    target=REPO/'build/shader-rebuild-poc'/case
    reference=REPO/'build/shader-rebuild-poc/full-source-555'
    available=json.loads(Path(profile_path).read_bytes()) if profile_path else profile
    available_keys={p['key_le_hex'] for p in available['pipelines']
                    if all(available['shaders'][available['environments'][e]['shader']]['references'] for e in p['environments'])}
    def located(p): return p['key_le_hex'] in available_keys
    if resolved_only: selected=[c for c in selected if located(c['pipeline'])]
    graphics=compare(trace(reference,deps,'key-disk'),trace(target,deps,'key-disk'),selected,profile['environments'])
    live=compare(trace(CAPTURE,deps,'key-live'),trace(target,deps,'key-disk'),selected,profile['environments'])
    a,b=compute_trace(reference),compute_trace(target)
    compute_masks=collections.defaultdict(int)
    for p in profile['pipelines']:
        if p['kind']!='compute': continue
        h=struct.unpack_from('<Q',bytes.fromhex(p['key_le_hex']))[0]
        for eid in p['environments']:
            for query,value in profile['environments'][eid]['cbuf_values']:
                bank,offset=query>>32,query&0xffffffff
                if offset==0xffffffff and bank<32: compute_masks[h]|=1<<bank
    def compute_identity(original):
        mask=compute_masks[struct.unpack_from('<Q',original)[0]]
        return normalize_compute_key(original,mask)
    outcomes=[]
    for index,p in enumerate(profile['pipelines']):
        if p['kind']!='compute' or resolved_only and not located(p): continue
        original=bytes.fromhex(p['key_le_hex']); key=compute_identity(original)
        aa,bb=a.get(key,{}),b.get(key,{})
        item=dict(index=index,key_equal=bool(aa and bb),unused_cbuf_sizes_normalized=key!=original)
        for kind,label in [('ir','ir'),('compute-spirv','spirv')]:
            hashes_a={digest(data) for data,path in aa.get(kind,[])}
            hashes_b={digest(data) for data,path in bb.get(kind,[])}
            item[label+'_equal']=len(hashes_a)==1 and hashes_a==hashes_b
            item[label+'_source_hashes']=sorted(hashes_a); item[label+'_target_hashes']=sorted(hashes_b)
        item['source_native_success']=len(aa.get('compute-native-success',[]))
        item['target_native_success']=len(bb.get('compute-native-success',[]))
        item['success']=item['key_equal'] and item['ir_equal'] and item['spirv_equal'] and item['source_native_success']>0 and item['target_native_success']>0
        outcomes.append(item)
    requested_keys={compute_identity(bytes.fromhex(profile['pipelines'][x['index']]['key_le_hex'])) for x in outcomes}
    compute=dict(total=len(outcomes),successes=sum(x['success'] for x in outcomes),
                 distinct_normalized_keys=len(requested_keys),
                 source_native_creations=sum(len(a.get(k,{}).get('compute-native-success',[])) for k in requested_keys),
                 target_native_creations=sum(len(b.get(k,{}).get('compute-native-success',[])) for k in requested_keys),outcomes=outcomes)
    report=dict(case=case,graphics_vs_normal_warmup=graphics,graphics_vs_live=live,compute=compute)
    (WORK/(case+'-comparison.json')).write_text(json.dumps(report,indent=2))
    print(json.dumps(dict(case=case,graphics={k:v for k,v in graphics.items() if k!='outcomes'},
                         graphics_vs_live={k:v for k,v in live.items() if k!='outcomes'},
                         compute={k:v for k,v in compute.items() if k!='outcomes'}),indent=2))
    return report


if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('case');p.add_argument('--resolved-only',action='store_true')
    p.add_argument('--profile')
    args=p.parse_args();run(args.case,args.resolved_only,args.profile)
