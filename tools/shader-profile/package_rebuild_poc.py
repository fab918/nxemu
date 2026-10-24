"""Whitelist-only deliverable: metadata and open source tools, never private traces/caches."""
import csv
import io
import json
from pathlib import Path
import shutil
import zipfile
from nxshader import load_profile, sha
from rebuild_recipes import REPO, WORK

destination=Path(r'C:\Users\fabri\Downloads\Patch GRAPH\NXEmu-Shader-Reconstruction-POC-20261022')
destination.mkdir(exist_ok=False)
files={
    'RAPPORT.md':REPO/'tools/shader-profile/RECONSTRUCTION_POC.md',
    'MarioKart-reconstruction.nxshader':WORK/'MarioKart-reconstruction.nxshader',
    'verified-results.json':WORK/'verified-results.json',
    'recipe-reference-comparison.json':WORK/'recipe-reference-comparison.json',
    'manifest-patch-replay-comparison.json':WORK/'manifest-patch-replay-comparison.json',
    'recipe-gaps.json':WORK/'recipe-gaps.json',
    'missing-shader-diagnosis.json':WORK/'missing-shader-diagnosis.json',
    'preparation-summary.json':WORK/'preparation-summary.json',
    'physical-resource-summary.json':WORK/'physical-resource-summary.json',
}
sources=['nxshader.py','game_resources.py','scan_compressed.py','rebuild_recipes.py',
         'resolve_recipe_resources.py','prepare_rebuild_trace.py','run_rebuild_case.py',
         'recipe_manifest.py','compare_rebuild_trace.py','diagnose_recipe_gaps.py',
         'scan_physical_yaz0.py','analyze_missing_shader_bytes.py','verify_rebuild_results.py',
         'test_nxshader.py','test_rebuild_poc.py','yaz0.cpp',
         'tests/CMakeLists.txt','tests/recipe_projection.cpp','tests/guest_pipeline_state.cpp',
         'tests/legacy_fixed_state.inc']
for relative in sources: files['source/tools/shader-profile/'+relative]=REPO/'tools/shader-profile'/relative
for name in ('fixed_pipeline_state.h','fixed_pipeline_state.cpp'):
    files['source/src/yuzu_video_core/renderer_vulkan/'+name]=REPO/'src/yuzu_video_core/renderer_vulkan'/name
for path in REPO.iterdir():
    if path.is_file() and path.name.lower().startswith(('license','copying')): files['source/'+path.name]=path
for name,source in files.items():
    target=destination/name; target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(source,target)
manifest=load_profile(WORK/'MarioKart-reconstruction.nxshader')
with (destination/'resource-references.csv').open('w',newline='',encoding='utf-8-sig') as file:
    writer=csv.DictWriter(file,fieldnames=['shader_sha256','game_file','encoding','physical_offset',
        'physical_size','decoded_offset','shader_size','resource','container_sha256'])
    writer.writeheader()
    for digest,shader in manifest['shaders'].items():
        for ref in shader['references']:
            compressed=ref['encoding']=='yaz0'
            writer.writerow(dict(shader_sha256=digest,game_file=ref['file'],encoding=ref['encoding'],
                physical_offset=ref['container_offset'] if compressed else ref['offset'],
                physical_size=ref['container_size'] if compressed else ref['size'],
                decoded_offset=ref['offset'] if compressed else '',shader_size=ref['size'],
                resource=ref.get('resource',''),container_sha256=ref.get('container_sha256','')))
archive=destination.with_suffix('.zip')
with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as out:
    for file in sorted(destination.rglob('*')):
        if file.is_file():
            assert file.suffix.lower() not in ('.bin','.spv','.dll','.exe','.spvasm','.diff')
            out.write(file,file.relative_to(destination))
with zipfile.ZipFile(archive) as check:
    assert check.testzip() is None
    assert sha(check.read('MarioKart-reconstruction.nxshader'))==sha((WORK/'MarioKart-reconstruction.nxshader').read_bytes())
print(json.dumps(dict(archive=str(archive),bytes=archive.stat().st_size,files=len(files)+1,
                      manifest_bytes=(destination/'MarioKart-reconstruction.nxshader').stat().st_size),indent=2))
