"""Create independent runs with identical copied saves/settings; never overwrite a run."""
import json
from pathlib import Path
import shutil
import sys
import zipfile

repo = Path(__file__).resolve().parents[2]
work = repo/'build'/'shader-profile-poc'
label = sys.argv[1]
if label not in ('A-empty', 'B-macos', 'C-manifest', 'D-local', 'E-local-v20'):
    raise ValueError('unknown POC run label')
run = work/'runs'/label
run.mkdir(parents=True, exist_ok=False)
runtime = work/'runtime'
for name in ('nxemu-shader-poc.exe', 'SDL2.dll'):
    shutil.copy2(runtime/name, run/name)
shutil.copytree(runtime/'ui', run/'ui')
user = run/'user'
user.mkdir()
for name in ('nand', 'game_content', 'game_library'):
    shutil.copytree(repo/'bin'/'x64'/'Release'/'user'/name, user/name)
(user/'portable.txt').touch()
(user/'config').mkdir()
config = json.loads((runtime/'user'/'config'/'NxEmu.config').read_text())
config['UI']['ConfirmBeforeStopping'] = False
buttons = config['nxemu-os']['controller']['player_0']['Button']
for name, code in dict(A=13, B=8, L=81, R=69, Plus=32, DUp=38, DDown=40, DLeft=37, DRight=39).items():
    buttons[name] = f'engine:keyboard,code:{code},toggle:0'
(user/'config'/'NxEmu.config').write_text(json.dumps(config, indent=2))
shader = user/'shader'/'0100152000022000'
shader.mkdir(parents=True)
if label == 'B-macos':
    with zipfile.ZipFile(r'C:\Users\fabri\Downloads\MarioKartMacvulkan.bin.zip') as z:
        (shader/'vulkan.bin').write_bytes(z.read('vulkan.bin'))
elif label == 'C-manifest':
    # This file was rebuilt by a separate import process using only .nxshader + game files.
    shutil.copy2(work/'reconstructed-resolved'/'vulkan.bin', shader/'vulkan.bin')
elif label == 'D-local':
    shutil.copy2(work/'original-mariokart-cache'/'vulkan.bin', shader/'vulkan.bin')
elif label == 'E-local-v20':
    shutil.copy2(work/'runs'/'A-empty'/'user'/'shader'/'0100152000022000'/'vulkan.bin', shader/'vulkan.bin')
print(run/'nxemu-shader-poc.exe')
