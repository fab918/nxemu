"""Compare actual live translation products with isolated recipe replay products."""
import collections
import hashlib
import json
from pathlib import Path
import re
from rebuild_recipes import CAPTURE, WORK, normalized, effective_size


def digest(data): return hashlib.sha256(data).hexdigest()


def trace(folder,deps,source):
    by_hash=collections.defaultdict(lambda:collections.defaultdict(list))
    for path in (folder/'trace').glob('*.bin'):
        match=re.fullmatch(r'([0-9a-f]{16})-(key-live|key-disk|ir|spirv|native-success|compute-key-live|compute-key-disk|compute-spirv|compute-native-success)-(\d+)-(\d+)\.bin',path.name)
        if not match: raise ValueError(f'unknown trace filename: {path.name}')
        h,kind,stage,sequence=match.groups()
        by_hash[h][kind].append((int(stage),int(sequence),path))
    result=collections.defaultdict(lambda:collections.defaultdict(list))
    for h,events in by_hash.items():
        keys=events.get(source,[])
        if not keys: continue
        identities=set()
        for _,_,path in keys:
            key=normalized(path.read_bytes(),deps)
            identities.add(key[:416+effective_size(key)])
        if len(identities)!=1: raise ValueError('hash maps to inconsistent effective keys')
        identity=identities.pop()
        for kind,items in events.items():
            for stage,seq,path in items:
                data=path.read_bytes()
                if kind=='ir':
                    # DumpProgram prints object addresses. Remove only those diagnostic addresses.
                    data=re.sub(rb'(?m)^\[[0-9a-fA-F]{16}\] ',b'',data)
                    if b'unknown block' in data: raise ValueError('IR contains an unresolved block pointer')
                result[identity][kind].append(dict(stage=stage,sha256=digest(data),size=len(data),
                                                   path=str(path.relative_to(folder))))
    return result


def compare(reference,target,selected,environments):
    outcomes=[]
    for c in selected:
        key=bytes.fromhex(c['pipeline']['key_le_hex'])
        identity=key[:416+effective_size(key)]
        a,b=reference.get(identity,{}),target.get(identity,{})
        # The existing renderer shares HLE/no-HLE twins only when no replacement was read.
        # Preserve the requested recipe, but compare the products of the actual shared pipeline.
        hint=(int.from_bytes(key[420:424],'little')>>17)&7
        may_alias=hint==1 and all(not environments[e]['cbuf_replacements'] for e in c['pipeline']['environments'])
        twin=bytearray(identity)
        word=int.from_bytes(twin[420:424],'little') & ~(7<<17)
        twin[420:424]=word.to_bytes(4,'little'); twin=bytes(twin)
        aa,bb=a,b
        live_alias=replay_alias=False
        if may_alias and not a.get('native-success') and reference.get(twin,{}).get('native-success'):
            aa=reference[twin]; live_alias=True
        if may_alias and not b.get('native-success') and target.get(twin,{}).get('native-success'):
            bb=target[twin]; replay_alias=True
        expected_stages={i for i,h in enumerate(c['recipe']['shader_hashes']) if h}
        record=dict(recipe_id=c['recipe_id'],key_equal=bool(a and b),
                    requested_key_reconstructed=True,hle_without_replacements=may_alias,
                    live_uses_existing_twin=live_alias,replay_uses_existing_twin=replay_alias,
                    effective_native_key_equal=bool(aa and bb) and live_alias==replay_alias,
                    live_native_successes=len(aa.get('native-success',[])),
                    replay_native_successes=len(bb.get('native-success',[])),
                    live_native_events=[x['path'] for x in aa.get('native-success',[])],
                    replay_native_events=[x['path'] for x in bb.get('native-success',[])],stages=[])
        for stage in sorted(expected_stages):
            part=dict(stage=stage)
            for kind in ('ir','spirv'):
                hashes_a={x['sha256'] for x in aa.get(kind,[]) if x['stage']==stage}
                hashes_b={x['sha256'] for x in bb.get(kind,[]) if x['stage']==stage}
                part[kind+'_equal']=len(hashes_a)==1 and hashes_a==hashes_b
                part[kind+'_live_hashes']=sorted(hashes_a); part[kind+'_replay_hashes']=sorted(hashes_b)
            record['stages'].append(part)
        record['success']=(bool(aa and bb) and record['live_native_successes']>0 and
                           record['replay_native_successes']>0 and
                           all(p['ir_equal'] and p['spirv_equal'] for p in record['stages']))
        outcomes.append(record)
    return dict(total=len(outcomes),complete_successes=sum(c['success'] for c in outcomes),
                matching_keys=sum(c['key_equal'] for c in outcomes),
                requested_keys_reconstructed=sum(c['requested_key_reconstructed'] for c in outcomes),
                live_hle_aliases=sum(c['live_uses_existing_twin'] for c in outcomes),
                replay_hle_aliases=sum(c['replay_uses_existing_twin'] for c in outcomes),
                matching_effective_native_keys=sum(c['effective_native_key_equal'] for c in outcomes),
                unique_live_native_creations=len({p for c in outcomes for p in c['live_native_events']}),
                unique_replay_native_creations=len({p for c in outcomes for p in c['replay_native_events']}),
                matching_ir_stages=sum(s['ir_equal'] for c in outcomes for s in c['stages']),
                matching_spirv_stages=sum(s['spirv_equal'] for c in outcomes for s in c['stages']),
                total_stages=sum(len(c['stages']) for c in outcomes),outcomes=outcomes)


if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser(description=__doc__); p.add_argument('case'); p.add_argument('--resolved-only',action='store_true')
    p.add_argument('--profile')
    args=p.parse_args()
    prep=json.loads((WORK/'preparation.json').read_bytes())
    deps={int(k):v for k,v in prep['dependencies'].items()}
    selected=prep['selected']
    if args.resolved_only:
        profile=json.loads(Path(args.profile or WORK/'resolved-profile.json').read_bytes())
        selected=[c for c in selected if all(profile['shaders'][profile['environments'][e]['shader']]['references']
                                            for e in c['pipeline']['environments'])]
    report=compare(trace(CAPTURE,deps,'key-live'),
                   trace(WORK.parent/args.case,deps,'key-disk'),selected,prep['profile']['environments'])
    (WORK/(args.case+'-comparison.json')).write_text(json.dumps(report,indent=2))
    print(json.dumps({k:v for k,v in report.items() if k!='outcomes'},indent=2))
