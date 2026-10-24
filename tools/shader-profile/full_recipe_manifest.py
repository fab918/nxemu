"""555-entry reconstruction adapter, preserving all observed guest combinations."""
import argparse
import copy
import json
from pathlib import Path
import struct
import sys
from nxshader import (canonical,env_bytes,export_profile,load_profile,parse_cache,read_cache,
                      reconstruct,sha,write_new,write_profile,MAGIC,VERSION)
from rebuild_recipes import REPO,CACHE,RECIPES,project,masks,normalized,effective_size

WORK=REPO/'build/shader-bktr-poc'


def compute_key(recipe):
    return struct.pack('<QI3I8I2I',recipe['shader_hash'],recipe['shared_memory_size'],
                       *recipe['workgroup_size'],*recipe['cbuf_sizes'],recipe['texture_table_size'],0)


def prepare():
    profile=json.loads((WORK/'nso-resolved-profile.json').read_bytes())
    capture=json.loads(RECIPES.read_bytes()); n=capture['guest_state_word_count']
    graphics=[]
    for index,r in enumerate(capture['recipes']):
        w=r['words']
        graphics.append(dict(kind='graphics',guest_words=w[:n],shader_hashes=[w[i]|w[i+1]<<32 for i in range(n,n+12,2)],
                             cbuf_sizes=w[n+12:n+102],texture_pool=w[n+102:],observations=r['observations'],
                             source_recipe_id=index))
    deps=masks(profile); projected=project(graphics,WORK/'full-projection')
    by_key={}
    for recipe,(key,size) in zip(graphics,projected):
        key=normalized(key,deps); by_key.setdefault(key[:416+size],[]).append((recipe,key))
    all_recipes=[]; selected=[]
    for index,pipeline in enumerate(profile['pipelines']):
        source=bytes.fromhex(pipeline['key_le_hex'])
        if pipeline['kind']=='graphics':
            source=normalized(source,deps); identity=source[:416+effective_size(source)]
            matches=by_key.get(identity)
            if not matches: raise ValueError(f'cache entry {index} has no captured guest state')
            recipe,key=max(matches,key=lambda pair:pair[0]['observations'])
            assert key[:416+effective_size(key)]==identity
            pipeline['key_le_hex']=key.hex()
            selected.append(dict(recipe_id=index,recipe=recipe,pipeline=pipeline))
        else:
            values=struct.unpack('<QI3I8I2I',source)
            assert values[-1]==0
            recipe=dict(kind='compute',shader_hash=values[0],shared_memory_size=values[1],
                        workgroup_size=list(values[2:5]),cbuf_sizes=list(values[5:13]),texture_table_size=values[13])
            assert compute_key(recipe)==source
        all_recipes.append(recipe)
    profile['guest_recipe_probe']=dict(schema='nxshader-guest-state-probe-1',
        target_feature_bits_for_this_test=63,dependency_masks={str(k):v for k,v in deps.items()},
        recipes=all_recipes,observed_graphics_recipes=graphics,
        scope='all source cache entries plus all observed graphics combinations; isolated v1 adapter')
    write_new(WORK/'full-profile.json',canonical(profile))
    write_new(WORK/'full-selection.json',canonical(selected))
    codes={sha(code):code for envs,key,compute,bounds in parse_cache(read_cache(CACHE)) for meta,code in envs}
    out=bytearray(MAGIC+struct.pack('<I',VERSION))
    for p in profile['pipelines']:
        envs=[profile['environments'][e] for e in p['environments']]
        out+=struct.pack('<I',len(envs))
        for e in envs: out+=env_bytes(e,codes[e['shader']])
        out+=bytes.fromhex(p['key_le_hex'])
    assert len(parse_cache(out))==555
    write_new(WORK/'reference-recipe-555-vulkan.bin',out)
    sizes=write_profile(WORK/'MarioKart-full-555.nxshader',profile)
    summary=dict(entries=len(profile['pipelines']),graphics=len(selected),compute=len(all_recipes)-len(selected),
                 preserved_graphics_observations=len(graphics),shaders=len(profile['shaders']),
                 manifest_bytes=sizes[1],json_bytes=sizes[0],manifest_sha256=sha((WORK/'MarioKart-full-555.nxshader').read_bytes()))
    (WORK/'manifest-summary.json').write_text(json.dumps(summary,indent=2))
    print(json.dumps(summary,indent=2))


def import_only(manifest,root,output):
    output=Path(output).resolve()
    def audit(event,args):
        if event!='open' or not isinstance(args[0],(str,bytes)): return
        path=Path(args[0]).resolve(); name=path.name.lower(); writing=bool(args[2]&3)
        if name.endswith('.zip') or name=='vulkan.bin' or name.endswith('-vulkan.bin'):
            if path!=output or not writing: raise RuntimeError('source cache/ZIP access forbidden during import')
    sys.addaudithook(audit)
    p=load_profile(manifest); extra=p['guest_recipe_probe']
    if len(extra['recipes'])!=len(p['pipelines']): raise ValueError('recipe count mismatch')
    deps={int(k):v for k,v in extra['dependency_masks'].items()}
    graphics=[r for r in extra['recipes'] if r['kind']=='graphics']
    projections=iter(project(graphics,output.parent/'projection',extra['target_feature_bits_for_this_test']))
    for pipeline,r in zip(p['pipelines'],extra['recipes']):
        if r['kind']=='graphics': key=normalized(next(projections)[0],deps)
        elif r['kind']=='compute': key=compute_key(r)
        else: raise ValueError('invalid recipe kind')
        if key.hex()!=pipeline['key_le_hex']: raise ValueError('derived key mismatch')
        pipeline['key_le_hex']=key.hex()
    report=reconstruct(p,root,output,True,REPO/'build/shader-profile-poc/yaz0.dll')
    report.update(source_cache_reads_forbidden=True,keys_derived= len(p['pipelines']),
                  graphics_keys_projected=len(graphics),compute_keys_reconstructed=len(p['pipelines'])-len(graphics))
    output.with_suffix('.report.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report,indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    sub=parser.add_subparsers(dest='action',required=True); sub.add_parser('prepare')
    im=sub.add_parser('import'); im.add_argument('manifest'); im.add_argument('root'); im.add_argument('output')
    args=parser.parse_args()
    if args.action=='prepare': prepare()
    else: import_only(args.manifest,args.root,args.output)
