"""Additional control: the v20 cache generated in run A on this same GPU."""
import json
import os
from pathlib import Path
import subprocess
import sys
import time

repo = Path(__file__).resolve().parents[2]
work = repo/'build'/'shader-profile-poc'
subprocess.run([sys.executable, str(repo/'tools'/'shader-profile'/'nvidia_profile.py'),
                'verify', str(work/'nvidia-policy.json')], check=True)
subprocess.run([sys.executable, str(repo/'tools'/'shader-profile'/'stage_run.py'), 'E-local-v20'], check=True)
run = work/'runs'/'E-local-v20'
env = os.environ.copy()
env['NXEMU_POC_RUN_SECONDS'] = '90'
for key in ('VK_INSTANCE_LAYERS', 'VK_LAYER_PATH', 'VK_ADD_LAYER_PATH', 'NXEMU_VK_TRACE'):
    env.pop(key, None)
started = time.monotonic()
print('E-local-v20 started', flush=True)
with (run/'process.log').open('wb') as output:
    process = subprocess.Popen([str(run/'nxemu-shader-poc.exe'),
        r'D:\Emulation\switch\NXDev\NxRoom\decrypted\Mario Kart 8 Deluxe [0100152000022000][v0].dnsp'],
        cwd=run, env=env, stdout=output, stderr=output)
    try:
        status = process.wait(timeout=180)
    except subprocess.TimeoutExpired:
        process.kill()
        raise RuntimeError('local control failed to stop gracefully')
result = dict(run='E-local-v20', exit_code=status, process_seconds=round(time.monotonic()-started, 3),
              configured_ui_seconds=90, no_input=True)
if status:
    raise RuntimeError(result)
matrix = json.loads((work/'matrix.json').read_text())
matrix.append(result)
(work/'matrix.json').write_text(json.dumps(matrix, indent=2))
print(json.dumps(result), flush=True)
