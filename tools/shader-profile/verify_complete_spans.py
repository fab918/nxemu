"""Final private verification: canonical resource bytes versus original cached windows."""
import collections
import json
from nxshader import load_profile,parse_cache,read_cache,sha
from shader_span import without_extra_cache_word
from rebuild_recipes import REPO,CACHE

work=REPO/'build/shader-origin-poc'; previous=REPO/'build/shader-bktr-poc'
profile=load_profile(work/'MarioKart-resource-spans.nxshader')
before=json.loads((previous/'full-profile-logical.json').read_bytes())
first=json.loads((work/'trimmed-discovery.json').read_bytes())['changes']
last=json.loads((work/'remaining-discovery.json').read_bytes())['changes']
changes=dict(first,**last)
assert len(changes)==15
assert all(s['references'] for s in profile['shaders'].values())
assert profile['guest_recipe_probe']==before['guest_recipe_probe']
assert [p['key_le_hex'] for p in profile['pipelines']]==[p['key_le_hex'] for p in before['pipelines']]
assert len(profile['pipelines'])==555
original=parse_cache(read_cache(CACHE))
rebuilt=read_cache(work/'game-only-complete/vulkan.bin'); records=parse_cache(rebuilt)
assert len(original)==len(records)==555
changed_records=[]; changed_environment_occurrences=0
for index,(a,b) in enumerate(zip(original,records)):
    assert a[2]==b[2] and len(a[0])==len(b[0])
    changed=False
    for (old_meta,old_code),(new_meta,new_code) in zip(a[0],b[0]):
        h=sha(old_code); expected=dict(old_meta)
        if h in changes:
            assert new_code==without_extra_cache_word(old_code)
            assert sha(new_code)==changes[h]['shader']
            expected['guest_read_highest']-=8
            changed=True;changed_environment_occurrences+=1
        else:assert new_code==old_code
        assert new_meta==expected
    if changed:changed_records.append(index)
comparison=json.loads((previous/'complete-resource-spans-comparison.json').read_bytes())
assert comparison['graphics_vs_normal_warmup']['complete_successes']==545
assert comparison['graphics_vs_live']['complete_successes']==545
assert comparison['compute']['successes']==10
import_report=json.loads((work/'game-only-complete/vulkan.report.json').read_bytes())
assert import_report['rebuilt_pipelines']==555 and import_report['missing_pipeline_indices']==[]
env_allowed={'local_memory_size','texture_bound','guest_start_address','guest_read_lowest','guest_read_highest',
             'viewport_transform_state','stage','texture_types','texture_pixel_formats','cbuf_values',
             'cbuf_replacements','program_header_sha256','gp_passthrough_mask','workgroup_size','shared_memory_size','shader'}
ref_allowed={'file','offset','size','encoding','resource','resource_offset','resource_kind','offset_space',
             'container_offset','container_size','container_sha256','verified_shader_sha256',
             'base_file','base_nca','patch_nca','section','resource_size','resource_sha256','physical_spans',
             'decoded_size','segment_sha256','segment'}
assert set(profile)=={'format','version','cache_version','abi','title_id','content_label','source_cache_sha256',
                      'shaders','environments','pipelines','guest_recipe_probe'}
for e in profile['environments'].values():assert set(e)<=env_allowed
for h,s in profile['shaders'].items():
    assert set(s)=={'size','references'}
    for ref in s['references']:
        assert set(ref)<=ref_allowed
        for span in ref.get('physical_spans',[]):assert set(span)=={'file','offset','size'}
for p in profile['pipelines']:assert set(p)=={'kind','key_le_hex','environments'}
compact=lambda r:{k:v for k,v in r.items() if k!='outcomes'}
result=dict(rebuilt_entries=555,total_entries=555,reconstruction_percent=100,
            graphics=compact(comparison['graphics_vs_normal_warmup']),
            graphics_vs_live=compact(comparison['graphics_vs_live']),compute=compact(comparison['compute']),
            cache_blob_identities_before=530,resource_program_identities=len(profile['shaders']),
            normalized_source_blobs=len(changes),normalized_unique_programs=len({x['shader'] for x in changes.values()}),
            unique_source_tail_bytes_excluded=8*len(changes),affected_pipeline_entries=len(changed_records),
            normalized_environment_occurrences=changed_environment_occurrences,
            affected_pipeline_indices=changed_records,changes=changes,
            terminal_branches_and_all_program_instruction_bytes_preserved=True,
            only_environment_read_upper_bounds_reduced_by_one_word=True,
            pipeline_keys_unchanged=True,all_1108_observed_guest_recipes_preserved=True,
            no_instruction_or_patch_payload_fields_in_manifest=True,
            source_cache_reads_forbidden_during_import=import_report['source_cache_reads_forbidden'],
            manifest_bytes=(work/'MarioKart-resource-spans.nxshader').stat().st_size,
            manifest_sha256=sha((work/'MarioKart-resource-spans.nxshader').read_bytes()),
            rebuilt_cache_sha256=sha(rebuilt))
(work/'verified-results.json').write_text(json.dumps(result,indent=2))
print(json.dumps({k:v for k,v in result.items() if k not in ('changes','affected_pipeline_indices')},indent=2))
