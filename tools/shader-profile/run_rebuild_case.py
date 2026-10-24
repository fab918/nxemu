"""Private portable test run. No mutation of the installed renderer or caches."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('label')
parser.add_argument('--seconds',type=int,default=90)
parser.add_argument('--cache',type=Path)
parser.add_argument('--executable',type=Path,help='Optional isolated UI executable for module testing')
parser.add_argument('--capture',action='store_true')
args=parser.parse_args()
if not args.label.replace('-','').isalnum(): raise ValueError('invalid case label')
repo=Path(__file__).resolve().parents[2]
work=repo/'build/shader-profile-poc'
run=repo/'build/shader-rebuild-poc'/args.label
run.mkdir(exist_ok=False)
shutil.copy2(args.executable or work/'master/bin/x64/Release/nxemu.exe',run/'nxemu-shader-poc.exe')
shutil.copy2(work/'runtime/SDL2.dll',run/'SDL2.dll')
shutil.copytree(work/'runtime/ui',run/'ui')
user=run/'user'; user.mkdir()
for name in ('nand','game_content','game_library'):
    shutil.copytree(repo/'bin/x64/Release/user'/name,user/name)
(user/'portable.txt').touch(); (user/'config').mkdir()
config=json.loads((work/'runtime/user/config/NxEmu.config').read_bytes())
config['UI']['ConfirmBeforeStopping']=False
config['nxemu-video']['video']['use_vulkan_driver_pipeline_cache']=False
(user/'config/NxEmu.config').write_text(json.dumps(config,indent=2))
if args.cache:
    shader=user/'shader/0100152000022000'; shader.mkdir(parents=True)
    shutil.copy2(args.cache,shader/('vulkan.nxc' if args.cache.suffix=='.nxc' else 'vulkan.bin'))
trace=run/'trace'; trace.mkdir()
env=os.environ.copy()
env['NXEMU_SHADER_REBUILD_TRACE']=str(trace)
env['NXEMU_POC_RUN_SECONDS']=str(args.seconds)
env.pop('NXEMU_SHADER_RECIPE_PROBE',None)
if args.capture: env['NXEMU_SHADER_RECIPE_PROBE']=str(run/'guest-recipes.json')
for name in ('VK_INSTANCE_LAYERS','VK_LAYER_PATH','VK_ADD_LAYER_PATH',
             'NXEMU_VK_TRACE','NXEMU_GPU_DIAGNOSTICS','NXEMU_GFX_DIAGNOSTICS','NXEMU_GPU_CAPTURE'):
    env.pop(name,None)
game=r'D:\Emulation\switch\NXDev\NxRoom\decrypted\Mario Kart 8 Deluxe [0100152000022000][v0].dnsp'
started=time.monotonic()
with (run/'process.log').open('wb') as log:
    process=subprocess.Popen([str(run/'nxemu-shader-poc.exe'),game],cwd=run,env=env,stdout=log,stderr=log)
    try: status=process.wait(timeout=max(args.seconds+120,180))
    except subprocess.TimeoutExpired:
        process.kill(); process.wait(); raise RuntimeError('graceful shutdown failed')
result=dict(label=args.label,exit_code=status,seconds=time.monotonic()-started,
            configured_seconds=args.seconds,nxemu_cache_empty_at_start=args.cache is None,
            driver_disk_cache_isolated=False,nxemu_native_pipeline_cache=False)
(run/'result.json').write_text(json.dumps(result,indent=2))
print(json.dumps(result,indent=2))
if status: raise RuntimeError(result)
