"""Existing nxshader-poc v1 adapter for the isolated guest-recipe experiment."""
import argparse
import copy
import json
from pathlib import Path
import struct
import sys
from nxshader import (canonical, env_bytes, load_profile, parse_cache, read_cache,
                      reconstruct, sha, write_new, write_profile, MAGIC, VERSION)
from rebuild_recipes import CACHE, REPO, WORK, project, normalized


def sample_profile(preparation, profile):
    out=copy.deepcopy(profile)
    selected=preparation['selected']
    out['pipelines']=[copy.deepcopy(c['pipeline']) for c in selected]
    out['environments']={e:out['environments'][e] for p in out['pipelines'] for e in p['environments']}
    out['shaders']={e['shader']:out['shaders'][e['shader']] for e in out['environments'].values()}
    out['guest_recipe_probe']={
        'schema':'nxshader-guest-state-probe-1',
        'target_feature_bits_for_this_test':63,
        'dependency_masks':preparation['dependencies'],
        'recipes':[dict(c['recipe'],source_recipe_id=c['recipe_id']) for c in selected],
        'scope':'isolated experimental adapter; not a new public format'}
    return out


def prepare():
    prep=json.loads((WORK/'preparation.json').read_bytes())
    profile=sample_profile(prep,prep['profile'])
    codes={sha(code):code for envs,key,compute,bounds in parse_cache(read_cache(CACHE)) for meta,code in envs}
    data=bytearray(MAGIC+struct.pack('<I',VERSION))
    for p in profile['pipelines']:
        envs=[profile['environments'][e] for e in p['environments']]
        data += struct.pack('<I',len(envs))
        for env in envs: data += env_bytes(env,codes[env['shader']])
        data += bytes.fromhex(p['key_le_hex'])
    assert len(parse_cache(data))==len(profile['pipelines'])
    write_new(WORK/'recipe-reference-vulkan.bin',data)
    print(json.dumps(dict(sample=len(profile['pipelines']),cache_sha256=sha(data),bytes=len(data))))


def export(profile_path=None, output_path=None):
    prep=json.loads((WORK/'preparation.json').read_bytes())
    profile=json.loads(Path(profile_path or WORK/'resolved-profile.json').read_bytes())
    output=sample_profile(prep,profile)
    sizes=write_profile(output_path or WORK/'MarioKart-recipes-sample.nxshader',output)
    print(json.dumps(dict(json_bytes=sizes[0],manifest_bytes=sizes[1],pipelines=len(output['pipelines']),
                          shaders=len(output['shaders']),environments=len(output['environments']))))


def import_only(manifest,game_root,output):
    # Audit is installed before opening the manifest. Every source vulkan.bin and ZIP is denied.
    output=Path(output).resolve()
    def audit(event,args):
        if event!='open' or not isinstance(args[0],(str,bytes)): return
        path=Path(args[0]).resolve()
        name=path.name.lower()
        flags=args[2]
        writing=bool(flags & 3)  # O_WRONLY or O_RDWR
        if name.endswith('.zip') or name=='vulkan.bin' or name.endswith('-vulkan.bin'):
            if path!=output or not writing:
                raise RuntimeError('source cache/ZIP read forbidden during manifest-only import')
    sys.addaudithook(audit)
    profile=load_profile(manifest)
    extra=profile['guest_recipe_probe']; recipes=extra['recipes']
    if len(recipes)!=len(profile['pipelines']): raise ValueError('recipe count mismatch')
    deps={int(k):v for k,v in extra['dependency_masks'].items()}
    projected=project(recipes,output.parent/'projection',extra['target_feature_bits_for_this_test'])
    for p,(key,size) in zip(profile['pipelines'],projected):
        key=normalized(key,deps)
        if key.hex()!=p['key_le_hex']: raise ValueError('projected key differs from manifest expectation')
        # Import uses the reconstructed key, not a raw key borrowed from the source cache.
        p['key_le_hex']=key.hex()
    report=reconstruct(profile,game_root,output,True,REPO/'build/shader-profile-poc/yaz0.dll')
    report.update(source_cache_reads_forbidden=True,keys_reprojected_from_guest_recipes=len(projected),
                  inputs='manifest + local game resources; local projection/Yaz0 tools only')
    output.with_suffix('.report.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report,indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    sub=parser.add_subparsers(dest='action',required=True)
    sub.add_parser('prepare')
    ex=sub.add_parser('export'); ex.add_argument('--profile'); ex.add_argument('--output')
    im=sub.add_parser('import'); im.add_argument('manifest'); im.add_argument('game_root'); im.add_argument('output')
    args=parser.parse_args()
    if args.action=='prepare': prepare()
    elif args.action=='export': export(args.profile,args.output)
    else: import_only(args.manifest,args.game_root,args.output)
