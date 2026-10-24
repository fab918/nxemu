"""Independent verification AFTER manifest-only import; comparisons may read private caches."""
import collections
import json
from pathlib import Path
from nxshader import load_profile, parse_cache, read_cache, sha, MAGIC, VERSION
from rebuild_recipes import REPO, WORK
import struct

manifest_path=WORK/'MarioKart-reconstruction.nxshader'
manifest=load_profile(manifest_path)
allowed={'format','version','cache_version','abi','title_id','content_label','source_cache_sha256',
         'shaders','environments','pipelines','guest_recipe_probe'}
assert set(manifest)==allowed
env_allowed={'local_memory_size','texture_bound','guest_start_address','guest_read_lowest','guest_read_highest',
             'viewport_transform_state','stage','texture_types','texture_pixel_formats','cbuf_values',
             'cbuf_replacements','program_header_sha256','gp_passthrough_mask','workgroup_size','shared_memory_size','shader'}
ref_allowed={'file','offset','size','encoding','resource','resource_offset','resource_kind','offset_space',
             'container_offset','container_size','container_sha256','verified_shader_sha256'}
for env in manifest['environments'].values(): assert set(env)<=env_allowed
for p in manifest['pipelines']:
    assert set(p)=={'kind','key_le_hex','environments'} and len(bytes.fromhex(p['key_le_hex']))==1400
for digest,s in manifest['shaders'].items():
    assert set(s)=={'size','references'} and len(digest)==64
    for ref in s['references']: assert set(ref)<=ref_allowed

source=read_cache(WORK/'recipe-reference-vulkan.bin')
original=parse_cache(source)
restored=read_cache(WORK.parent/'manifest-only-patch/vulkan.bin')
import_report=json.loads((WORK.parent/'manifest-only-patch/vulkan.report.json').read_bytes())
missing=set(import_report['missing_pipeline_indices'])
expected=bytearray(MAGIC+struct.pack('<I',VERSION))
for i,record in enumerate(original):
    if i not in missing: expected+=source[record[3][0]:record[3][1]]
assert restored==expected
assert len(parse_cache(restored))==61

# Verify optional compressed-container fingerprints, independently of shader fingerprints.
root=Path(r'D:\Emulation\switch\NXDev\NxRoom\decrypted')
containers={}
for s in manifest['shaders'].values():
    for ref in s['references']:
        if 'container_sha256' in ref:
            containers[(ref['file'],ref['container_offset'],ref['container_size'])]=ref['container_sha256']
for (name,offset,size),digest in containers.items():
    with (root/name).open('rb') as f:
        f.seek(offset); assert sha(f.read(size))==digest

profile=json.loads((WORK/'physical-resolved-profile.json').read_bytes())
prep=json.loads((WORK/'preparation.json').read_bytes())
preparation_summary=json.loads((WORK/'preparation-summary.json').read_bytes())
gaps=json.loads((WORK/'recipe-gaps.json').read_bytes())
resource=json.loads((WORK/'physical-resource-summary.json').read_bytes())
shader_diagnosis={x['shader_sha256']:x for x in json.loads((WORK/'missing-shader-diagnosis.json').read_bytes())}
failures=[]
for i in sorted(missing):
    c=prep['selected'][i]; entries=[]
    for eid in c['pipeline']['environments']:
        env=profile['environments'][eid]; digest=env['shader']
        if not profile['shaders'][digest]['references']:
            entries.append(dict(sha256=digest,stage=env['stage'],size=profile['shaders'][digest]['size'],
                                classification=shader_diagnosis[digest]['classification']))
    failures.append(dict(sample_index=i,source_recipe_id=c['recipe_id'],unlocated_shaders=entries))
counts=collections.Counter()
for p in profile['pipelines']:
    counts[p['kind']+'_total']+=1
    if all(profile['shaders'][profile['environments'][e]['shader']]['references'] for e in p['environments']):
        counts[p['kind']+'_locatable']+=1
p0=json.loads((WORK/'recipe-reference-comparison.json').read_bytes())
p2=json.loads((WORK/'manifest-patch-replay-comparison.json').read_bytes())
assert p0['complete_successes']==64 and p2['complete_successes']==61
result=dict(manifest_bytes=manifest_path.stat().st_size,manifest_sha256=sha(manifest_path.read_bytes()),
            proprietary_instruction_fields_absent=True,manifest_schema_whitelist_verified=True,
            source_cache_reads_forbidden_during_import=import_report['source_cache_reads_forbidden'],
            restored_cache_byte_identical_to_reference_subset=True,restored_cache_sha256=sha(restored),
            compressed_container_fingerprints_verified=len(containers),
            captured_recipes=preparation_summary['recipes'],direct_recipe_joins=preparation_summary['joined_recipes'],
            explained_hle_alias_gaps=sum(x.get('no_cbuf_replacements',False) for x in gaps),
            p0={k:v for k,v in p0.items() if k!='outcomes'},
            p2={k:v for k,v in p2.items() if k!='outcomes'},
            sample_shader_total=len(manifest['shaders']),sample_shader_locatable=import_report['resolved_shaders'],
            full_capture_shader_total=len(profile['shaders']),full_capture_shader_locatable=resource['resolved_shaders'],
            full_capture_pipeline_localization=dict(counts),
            failures=failures)
(WORK/'verified-results.json').write_text(json.dumps(result,indent=2))
print(json.dumps(result,indent=2))
