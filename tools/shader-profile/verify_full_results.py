"""Independent final evidence checks; private source-cache reads happen only here."""
import collections
import json
import struct
from nxshader import load_profile,read_cache,parse_cache,sha,MAGIC,VERSION
from full_recipe_manifest import WORK
from rebuild_recipes import CACHE

manifest=load_profile(WORK/'MarioKart-full-555-logical.nxshader')
summary=json.loads((WORK/'final-manifest-summary.json').read_bytes())
source=read_cache(WORK/'reference-recipe-555-vulkan.bin')
records=parse_cache(source)
golden=parse_cache(read_cache(CACHE))
assert len(records)==len(golden)==len(manifest['pipelines'])==555
for a,b in zip(records,golden): assert a[0]==b[0] and a[2]==b[2]
restored=read_cache(WORK/'game-only-logical/vulkan.bin')
imported=json.loads((WORK/'game-only-logical/vulkan.report.json').read_bytes())
missing=set(imported['missing_pipeline_indices'])
expected=bytearray(MAGIC+struct.pack('<I',VERSION))
for index,record in enumerate(records):
    if index not in missing: expected+=source[record[3][0]:record[3][1]]
assert restored==expected
assert restored==read_cache(WORK/'game-only/vulkan.bin')
assert len(parse_cache(restored))==530

env_allowed={'local_memory_size','texture_bound','guest_start_address','guest_read_lowest','guest_read_highest',
             'viewport_transform_state','stage','texture_types','texture_pixel_formats','cbuf_values',
             'cbuf_replacements','program_header_sha256','gp_passthrough_mask','workgroup_size','shared_memory_size','shader'}
ref_allowed={'file','offset','size','encoding','resource','resource_offset','resource_kind','offset_space',
             'container_offset','container_size','container_sha256','verified_shader_sha256',
             'base_file','base_nca','patch_nca','section','resource_size','resource_sha256','physical_spans',
             'decoded_size','segment_sha256','segment'}
assert set(manifest)=={'format','version','cache_version','abi','title_id','content_label','source_cache_sha256',
                       'shaders','environments','pipelines','guest_recipe_probe'}
for e in manifest['environments'].values():assert set(e)<=env_allowed
for p in manifest['pipelines']:
    assert set(p)=={'kind','key_le_hex','environments'}
    assert len(bytes.fromhex(p['key_le_hex']))==(1400 if p['kind']=='graphics' else 64)
for h,s in manifest['shaders'].items():
    assert set(s)=={'size','references'} and len(h)==64
    for ref in s['references']:
        assert set(ref)<=ref_allowed
        for span in ref.get('physical_spans',[]): assert set(span)=={'file','offset','size'}
assert len(manifest['guest_recipe_probe']['recipes'])==555
assert len(manifest['guest_recipe_probe']['observed_graphics_recipes'])==1108
for r in manifest['guest_recipe_probe']['observed_graphics_recipes']:
    assert len(r['guest_words'])==278 and r['guest_words'][0]==1 and not r['guest_words'][1]&63

reference=json.loads((WORK/'full-recipe-555-comparison.json').read_bytes())
game=json.loads((WORK/'full-game-only-comparison.json').read_bytes())
assert reference['graphics_vs_normal_warmup']['complete_successes']==545
assert reference['graphics_vs_live']['complete_successes']==545
assert reference['compute']['successes']==10
assert game['graphics_vs_normal_warmup']['complete_successes']==520
assert game['graphics_vs_live']['complete_successes']==520
assert game['compute']['successes']==10
def compact(report):
    return {key:{k:v for k,v in value.items() if k!='outcomes'} for key,value in report.items() if isinstance(value,dict)}
unlocated={h:dict(size=s['size'],stages=sorted({e['stage'] for e in manifest['environments'].values() if e['shader']==h}))
           for h,s in manifest['shaders'].items() if not s['references']}
failures=[dict(index=i,kind=manifest['pipelines'][i]['kind'],
               missing_shaders=sorted({manifest['environments'][e]['shader'] for e in manifest['pipelines'][i]['environments']
                                        if manifest['environments'][e]['shader'] in unlocated})) for i in sorted(missing)]
result=dict(manifest=summary,source_cache_entry_validation=compact(reference),game_only_entry_validation=compact(game),
            source_cache_reads_forbidden_during_import=imported['source_cache_reads_forbidden'],
            imported_cache_identical_to_verified_reference_subset=True,
            imported_cache_identical_to_gpu_validated_input=True,imported_cache_sha256=sha(restored),
            manifest_fields_audited_without_instruction_blobs=True,
            resolved_shaders=imported['resolved_shaders'],total_shaders=imported['total_shaders'],
            game_only_rebuilt=imported['rebuilt_pipelines'],total_entries=555,
            bktr=json.loads((WORK/'bktr-summary.json').read_bytes()),
            logical_shader_checks={k:v for k,v in json.loads((WORK/'bktr-locator-verification.json').read_bytes()).items() if k!='references'},
            remaining_part_search={k:sum(bool(p[k]) for p in json.loads((WORK/'romfs-parts.json').read_bytes()).values())
                                   for k in ('header','body')},
            unlocated_shaders=unlocated,failures=failures)
(WORK/'verified-results.json').write_text(json.dumps(result,indent=2))
print(json.dumps(dict(manifest_bytes=summary['manifest_bytes'],source_entries_validated=555,
                      game_entries_validated=530,game_percent=530/555*100,shaders_located=515,
                      shaders_missing=len(unlocated),pipelines_missing=len(missing),
                      final_import_sha256=sha(restored)),indent=2))
