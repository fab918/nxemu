"""Package only metadata and source files; exclude shader snapshots and compiled products."""
import csv
import json
from pathlib import Path
import shutil
import zipfile
from nxshader import load_profile,sha
from full_recipe_manifest import WORK
from rebuild_recipes import REPO

destination=Path(r'C:\Users\fabri\Downloads\Patch GRAPH\NXEmu-Shader-BKTR-Full-POC-20261023')
destination.mkdir(exist_ok=False)
files={'RAPPORT.md':REPO/'tools/shader-profile/FULL_BKTR_POC.md',
       'MarioKart-full-555-logical.nxshader':WORK/'MarioKart-full-555-logical.nxshader'}
for name in ['verified-results.json','final-manifest-summary.json','bktr-summary.json','nso-summary.json',
             'bktr-locator-verification.json','full-recipe-555-comparison.json','full-game-only-comparison.json',
             'romfs-parts.json','nso-parts.json']:
    files[name]=WORK/name
sources=['nxshader.py','game_resources.py','bktr_resources.py','nso_resources.py',
         'scan_compressed.py','rebuild_recipes.py','recipe_manifest.py','full_recipe_manifest.py',
         'prepare_rebuild_trace.py','prepare_compute_trace.py','run_rebuild_case.py',
         'compare_rebuild_trace.py','compare_full_replay.py','scan_bktr_resources.py','scan_nso_shaders.py',
         'locate_nso_parts.py','locate_romfs_parts.py','validate_bktr_locators.py','finalize_full_manifest.py',
         'verify_full_results.py','test_nxshader.py','test_rebuild_poc.py','test_bktr_resources.py',
         'test_nso_resources.py','test_compute_comparison.py','yaz0.cpp',
         'tests/CMakeLists.txt','tests/recipe_projection.cpp','tests/guest_pipeline_state.cpp','tests/legacy_fixed_state.inc']
for name in sources:files['source/tools/shader-profile/'+name]=REPO/'tools/shader-profile'/name
for name in ['fixed_pipeline_state.cpp','fixed_pipeline_state.h']:
    files['source/src/yuzu_video_core/renderer_vulkan/'+name]=REPO/'src/yuzu_video_core/renderer_vulkan'/name
for source in REPO.iterdir():
    if source.is_file() and source.name.lower().startswith(('license','copying')):files['source/'+source.name]=source
for name,source in files.items():
    target=destination/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
profile=load_profile(WORK/'MarioKart-full-555-logical.nxshader')
with (destination/'resource-references.csv').open('w',encoding='utf-8-sig',newline='') as f:
    columns=['shader_sha256','file','base_file','encoding','physical_offset','physical_size',
             'logical_resource_offset','logical_resource_size','shader_offset','shader_size',
             'resource','resource_sha256','container_sha256','segment_sha256']
    writer=csv.DictWriter(f,fieldnames=columns);writer.writeheader()
    for h,s in profile['shaders'].items():
        for ref in s['references']:
            logical=ref['encoding'].startswith('bktr-');raw=ref['encoding']=='raw'
            writer.writerow(dict(shader_sha256=h,file=ref['file'],base_file=ref.get('base_file',''),encoding=ref['encoding'],
                physical_offset='' if logical else ref['offset'] if raw else ref['container_offset'],
                physical_size='' if logical else ref['size'] if raw else ref['container_size'],
                logical_resource_offset=ref['resource_offset'] if logical else '',
                logical_resource_size=ref['resource_size'] if logical else '',
                shader_offset='' if raw else ref['offset'],shader_size=ref['size'],resource=ref.get('resource',''),
                resource_sha256=ref.get('resource_sha256',''),container_sha256=ref.get('container_sha256',''),
                segment_sha256=ref.get('segment_sha256','')))
archive=destination.with_suffix('.zip')
with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as out:
    for file in sorted(destination.rglob('*')):
        if not file.is_file():continue
        assert file.suffix.lower() not in ('.bin','.spv','.spvasm','.exe','.dll','.diff')
        out.write(file,file.relative_to(destination))
with zipfile.ZipFile(archive) as check:
    assert check.testzip() is None
    assert sha(check.read('MarioKart-full-555-logical.nxshader'))==sha((WORK/'MarioKart-full-555-logical.nxshader').read_bytes())
print(json.dumps(dict(archive=str(archive),bytes=archive.stat().st_size,files=len(files)+1,
                      manifest_bytes=(destination/'MarioKart-full-555-logical.nxshader').stat().st_size),indent=2))
