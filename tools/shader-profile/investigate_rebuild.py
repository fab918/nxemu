"""Keep detailed differences private under build/; metadata summary has no code."""
import difflib
import json
from pathlib import Path
import struct
import subprocess
from compare_rebuild_trace import trace
from rebuild_recipes import CAPTURE, WORK, normalized, effective_size

prep=json.loads((WORK/'preparation.json').read_bytes())
deps={int(k):v for k,v in prep['dependencies'].items()}
baseline=trace(CAPTURE,deps,'key-live')
run=WORK.parent/'recipe-reference'
target=trace(run,deps,'key-disk')
report=json.loads((WORK/'recipe-reference-comparison.json').read_bytes())
private=WORK/'private-differences'; private.mkdir(exist_ok=True)
summary=[]
for c,outcome in zip(prep['selected'],report['outcomes']):
    if outcome['success']: continue
    key=bytes.fromhex(c['pipeline']['key_le_hex']); identity=key[:416+effective_size(key)]
    item=dict(recipe_id=c['recipe_id'],source_pipeline_index=c['source_pipeline_index'],
              baseline_key_present=identity in baseline,replay_key_present=identity in target,
              requested_app_stage=(struct.unpack_from('<I',key,420)[0]>>17)&7,
              baseline_native=outcome['live_native_successes'],replay_native=outcome['replay_native_successes'])
    if identity not in target or identity not in baseline:
        nearest=[]
        for label,entries in [('live',baseline),('replay',target)]:
            candidates=[(sum(x!=y for x,y in zip(identity,k)),k) for k in entries if k[:48]==identity[:48]]
            for distance,k in sorted(candidates)[:2]:
                nearest.append(dict(source=label,distance=distance,
                                    differences=[dict(offset=i,requested=a,actual=b) for i,(a,b) in
                                                 enumerate(zip(identity,k)) if a!=b]))
        item['nearest_same_shaders']=nearest
    for part in outcome['stages']:
        if part['ir_equal'] and not part['spirv_equal']:
            aa=[x for x in baseline[identity]['spirv'] if x['stage']==part['stage']]
            bb=[x for x in target[identity]['spirv'] if x['stage']==part['stage']]
            if not aa or not bb:
                item.setdefault('no_spirv_emission',[]).append(dict(stage=part['stage'],live=len(aa),replay=len(bb)))
                continue
            a,b=aa[0],bb[0]
            inputs=[CAPTURE/a['path'],run/b['path']]
            dis=[]; compact=[]
            for i,path in enumerate(inputs):
                output=private/f'{c["recipe_id"]}-{part["stage"]}-{i}.spvasm'
                subprocess.run(['spirv-dis',str(path),'-o',str(output),'--raw-id'],check=True)
                dis.append(output.read_text().splitlines())
                canon=output.with_suffix('.compact.spv')
                subprocess.run(['spirv-opt','--compact-ids',str(path),'-o',str(canon)],check=True)
                compact.append(canon.read_bytes())
            diff='\n'.join(difflib.unified_diff(dis[0],dis[1],fromfile='live',tofile='replay'))
            (private/f'{c["recipe_id"]}-{part["stage"]}.diff').write_text(diff)
            item.setdefault('spirv_differences',[]).append(dict(stage=part['stage'],compact_ids_equal=compact[0]==compact[1],
                                                               diff_lines=len(diff.splitlines())))
    summary.append(item)
(WORK/'diagnosis.json').write_text(json.dumps(summary,indent=2))
print(json.dumps(summary,indent=2))
