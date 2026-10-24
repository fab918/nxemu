"""This-machine diagnostic run. No change to installed caches or driver settings."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

repo = Path(__file__).resolve().parents[2]
work = repo / 'build' / 'shader-profile-poc'
run = repo / 'build' / 'shader-recipe-poc' / 'mariokart-probe'
run.mkdir(exist_ok=False)
shutil.copy2(work / 'master/bin/x64/Release/nxemu.exe', run / 'nxemu-recipe-probe.exe')
shutil.copy2(work / 'runtime/SDL2.dll', run / 'SDL2.dll')
shutil.copytree(work / 'runtime/ui', run / 'ui')
user = run / 'user'
user.mkdir()
for name in ('nand', 'game_content', 'game_library'):
    shutil.copytree(repo / 'bin/x64/Release/user' / name, user / name)
(user / 'portable.txt').touch()
(user / 'config').mkdir()
config = json.loads((work / 'runtime/user/config/NxEmu.config').read_text())
config['UI']['ConfirmBeforeStopping'] = False
(user / 'config/NxEmu.config').write_text(json.dumps(config, indent=2))
env = os.environ.copy()
env['NXEMU_SHADER_RECIPE_PROBE'] = str(run / 'guest-recipes.json')
env['NXEMU_POC_RUN_SECONDS'] = '90'
for name in ('VK_INSTANCE_LAYERS', 'VK_LAYER_PATH', 'VK_ADD_LAYER_PATH',
             'NXEMU_VK_TRACE', 'NXEMU_GPU_DIAGNOSTICS', 'NXEMU_GFX_DIAGNOSTICS', 'NXEMU_GPU_CAPTURE'):
    env.pop(name, None)
game = Path(r'D:\Emulation\switch\NXDev\NxRoom\decrypted\Mario Kart 8 Deluxe [0100152000022000][v0].dnsp')
started = time.monotonic()
with (run / 'process.log').open('wb') as log:
    process = subprocess.Popen([str(run / 'nxemu-recipe-probe.exe'), str(game)], cwd=run,
                               env=env, stdout=log, stderr=log)
    try:
        status = process.wait(timeout=180)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
        raise RuntimeError('Diagnostic run did not stop gracefully')
result = dict(exit_code=status, seconds=time.monotonic()-started, no_input=True,
              driver_cache_isolation=False, diagnostic_only=True)
(run / 'result.json').write_text(json.dumps(result, indent=2))
if status:
    raise RuntimeError(result)
probe = json.loads((run / 'guest-recipes.json').read_text())
result.update({k: v for k, v in probe.items() if k != 'recipes'})
result['unique_guest_recipes'] = len(probe['recipes'])
print(json.dumps(result, indent=2))
if probe['projection_mismatches'] or probe['failed'] or probe['dropped_observations']:
    raise RuntimeError('Guest projection validation failed or incomplete')
