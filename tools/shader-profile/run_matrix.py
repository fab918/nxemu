"""Run four fixed-duration, no-input sessions sequentially, in private portable dirs."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

repo = Path(__file__).resolve().parents[2]
work = repo/'build'/'shader-profile-poc'
old = (work/'runs'/'A-empty').resolve()
if old.exists():
    if not old.is_relative_to(work.resolve()):
        raise ValueError('exploratory run outside workspace')
    old.rename(work/'runs'/'A-exploratory')
shutil.copy2(work/'master'/'bin'/'x64'/'Release'/'nxemu.exe', work/'runtime'/'nxemu-shader-poc.exe')
game = r'D:\Emulation\switch\NXDev\NxRoom\decrypted\Mario Kart 8 Deluxe [0100152000022000][v0].dnsp'
env = os.environ.copy()
env['NXEMU_POC_RUN_SECONDS'] = '90'
for key in ('VK_INSTANCE_LAYERS', 'VK_LAYER_PATH', 'VK_ADD_LAYER_PATH', 'NXEMU_VK_TRACE',
            'NXEMU_GPU_DIAGNOSTICS', 'NXEMU_GFX_DIAGNOSTICS', 'NXEMU_GPU_CAPTURE'):
    env.pop(key, None)
results = []
for label in ('A-empty', 'B-macos', 'C-manifest', 'D-local'):
    subprocess.run([sys.executable, str(repo/'tools'/'shader-profile'/'nvidia_profile.py'),
                    'verify', str(work/'nvidia-policy.json')], check=True)
    subprocess.run([sys.executable, str(repo/'tools'/'shader-profile'/'stage_run.py'), label], check=True)
    run = work/'runs'/label
    started = time.monotonic()
    print(json.dumps(dict(run=label, status='started', epoch=time.time())), flush=True)
    with (run/'process.log').open('wb') as output:
        process = subprocess.Popen([str(run/'nxemu-shader-poc.exe'), game], cwd=run, env=env,
                                   stdout=output, stderr=output)
        (run/'pid.txt').write_text(str(process.pid))
        try:
            status = process.wait(timeout=180)
        except subprocess.TimeoutExpired:
            process.kill()
            raise RuntimeError(f'{label}: graceful timeout; session invalid')
    result = dict(run=label, exit_code=status, process_seconds=round(time.monotonic()-started, 3),
                  configured_ui_seconds=90, no_input=True)
    results.append(result)
    (work/'matrix.json').write_text(json.dumps(results, indent=2))
    print(json.dumps(result), flush=True)
    if status:
        raise RuntimeError(f'{label}: process exited with {status}')
