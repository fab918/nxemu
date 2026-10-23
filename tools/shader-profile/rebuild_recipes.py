"""Isolated P0/P1 preparation; never change the installed game or renderer."""
import collections
import json
from pathlib import Path
import struct
import subprocess
from nxshader import export_profile, load_profile, canonical, sha, write_new

REPO = Path(__file__).resolve().parents[2]
WORK = REPO/'build/shader-rebuild-poc/validation'
CAPTURE = REPO/'build/shader-rebuild-poc/normal-cold'
CACHE = CAPTURE/'user/shader/0100152000022000/vulkan.bin'
RECIPES = CAPTURE/'guest-recipes.json'


def project(recipes, folder, feature_bits=63):
    folder.mkdir(parents=True, exist_ok=True)
    source, dest = folder/'projection-input.bin', folder/'projection-output.bin'
    out = bytearray(struct.pack('<I', len(recipes)))
    for r in recipes:
        w = r['guest_words']
        out += struct.pack('<I',len(w)) + struct.pack(f'<{len(w)}I',*w)
    source.write_bytes(out)
    tool = REPO/'build/shader-recipe-poc/tests/Release/recipe-projection.exe'
    info = json.loads(subprocess.check_output([str(tool),str(source),str(dest),str(feature_bits)]))
    raw, pos, result = dest.read_bytes(), 0, []
    assert info['count'] == len(recipes) and info['state_bytes'] == 984
    for r in recipes:
        size, = struct.unpack_from('<I',raw,pos)
        state = raw[pos+4:pos+4+984]
        pos += 4+984
        key = struct.pack('<6Q',*r['shader_hashes']) + struct.pack('<90I',*r['cbuf_sizes'])
        key += struct.pack('<2I',*r['texture_pool']) + state
        result.append((key,size))
    assert pos == len(raw)
    return result


def masks(profile):
    result = collections.defaultdict(int)
    for p in profile['pipelines']:
        if p['kind'] != 'graphics': continue
        hashes = [x for x in struct.unpack_from('<6Q',bytes.fromhex(p['key_le_hex'])) if x]
        for h,eid in zip(hashes,p['environments']):
            for key,value in profile['environments'][eid]['cbuf_values']:
                bank,offset = key>>32,key&0xffffffff
                if offset == 0xffffffff and bank<32: result[h] |= 1<<bank
                if key == 0xfffffffffffffffe: result[h] |= 1<<31
            result[h] |= 0
    return result


def normalized(key, dependencies):
    key = bytearray(key)
    hashes = struct.unpack_from('<6Q',key)
    for stage in range(5):
        mask = dependencies.get(hashes[stage+1],0) | (dependencies.get(hashes[0],0) if stage==0 else 0)
        for bank in range(18):
            if not mask & (1<<bank): struct.pack_into('<I',key,48+(stage*18+bank)*4,0)
    if not any(dependencies.get(h,0)&(1<<31) for h in hashes): key[408:416] = bytes(8)
    return bytes(key)


def effective_size(key):
    a,b = struct.unpack_from('<II',key,416)
    if a&64: return 984
    if a&32 and a&8: return 64
    if a&32: return 104
    if a&1 and not b&(1<<20): return 360
    return 424


def select_representative(candidates, limit=64):
    def tags(c):
        p,r = c['pipeline'],c['recipe']
        k=bytes.fromhex(p['key_le_hex']); a,b=struct.unpack_from('<II',k,416)
        envs=[c['environments'][x] for x in p['environments']]
        return {f'stages:{len(envs)}',f'topology:{a>>24&15}',f'app:{b>>17&7}',
                f'colors:{k[424:432].hex()}',f'dual_source:{b&1}',f'xfb:{bool(a&64)}',
                *(f'{name}:{bool(e[name])}' for e in envs for name in
                  ('texture_types','texture_pixel_formats','cbuf_values','cbuf_replacements')),
                *(f'stage:{e["stage"]}' for e in envs)}
    pending=list(candidates); selected=[]; covered=set()
    while pending and len(selected)<limit:
        best=max(pending,key=lambda c:(len(tags(c)-covered),c['recipe']['observations'],c['recipe_id']))
        selected.append(best); covered |= tags(best); pending.remove(best)
    return selected,sorted(covered)


def main():
    WORK.mkdir(parents=True,exist_ok=True)
    profile,codes,stats=export_profile(CACHE,'0100152000022000','Mario Kart 8 Deluxe + local update 3.0.3')
    raw=json.loads(RECIPES.read_bytes()); n=raw['guest_state_word_count']
    recipes=[]
    for r in raw['recipes']:
        w=r['words']; hashes=[w[i]|w[i+1]<<32 for i in range(n,n+12,2)]
        recipes.append(dict(guest_words=w[:n],shader_hashes=hashes,cbuf_sizes=w[n+12:n+102],
                            texture_pool=w[n+102:],observations=r['observations']))
    projected=project(recipes,WORK/'projection')
    deps=masks(profile)
    indexed=collections.defaultdict(list)
    for index,p in enumerate(profile['pipelines']):
        if p['kind'] != 'graphics': continue
        k=normalized(bytes.fromhex(p['key_le_hex']),deps)
        indexed[k[:416+effective_size(k)]].append((index,p,k))
    matches=[]; failed=[]; seen=set(); reasons=collections.Counter()
    for rid,(r,(key,size)) in enumerate(zip(recipes,projected)):
        key=normalized(key,deps)
        found=indexed.get(key[:416+size],[])
        signatures={tuple(p['environments']) for _,p,_ in found}
        if len(signatures)!=1:
            reason='no_cached_environment_for_projected_key' if not found else 'ambiguous_environments'
            reasons[reason]+=1; failed.append(dict(recipe=rid,reason=reason)); continue
        index,p,reference=found[0]
        assert key[:416+size] == reference[:416+effective_size(reference)]
        if key[:416+size] in seen: continue
        seen.add(key[:416+size])
        matches.append(dict(recipe_id=rid,recipe=r,source_pipeline_index=index,
                            pipeline=dict(p,key_le_hex=key.hex()),reference_key_le_hex=reference.hex(),
                            environments=profile['environments']))
    selected,coverage=select_representative(matches)
    prior=load_profile(REPO/'build/shader-profile-poc/MarioKart-cache-macOS.nxshader')
    previous=json.loads((REPO/'build/shader-rebuild-poc/resolved-profile.json').read_bytes())
    reused=0
    for h,s in profile['shaders'].items():
        refs=previous['shaders'].get(h,{}).get('references',[]) or prior['shaders'].get(h,{}).get('references',[])
        if refs: s['references']=refs; reused+=1
    # Private preparation data contain code only in cache paths, never in these JSON outputs.
    for c in selected: del c['environments']
    preparation=dict(profile=profile,selected=selected,all_joined_recipe_ids=[c['recipe_id'] for c in matches],
                     failures=failed,dependencies={str(k):v for k,v in deps.items()})
    write_new(WORK/'preparation.json',canonical(preparation))
    mac_hashes={h for p in prior['pipelines'] if p['kind']=='graphics'
                for h in struct.unpack_from('<6Q',bytes.fromhex(p['key_le_hex'])) if h}
    recipe_hashes={h for r in recipes for h in r['shader_hashes'] if h}
    summary=dict(source_cache=stats,recipes=len(recipes),joined_recipes=len(recipes)-len(failed),
                 joined_unique_local_keys=len(matches),failures=dict(reasons),sample_size=len(selected),
                 sample_coverage=coverage,prior_verified_references_reused=reused,
                 shaders=len(profile['shaders']),recipe_shader_hashes=len(recipe_hashes),
                 recipe_hashes_also_in_mac_cache=len(recipe_hashes&mac_hashes))
    (WORK/'preparation-summary.json').write_text(json.dumps(summary,indent=2))
    print(json.dumps(summary,indent=2))


if __name__=='__main__': main()
