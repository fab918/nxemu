"""Create a private portable runtime and back up/empty only Mario Kart's NXEmu caches."""
from pathlib import Path
import json
import shutil
import sys

repo = Path(sys.argv[1]).resolve()
work = repo/'build'/'shader-profile-poc'
runtime = work/'runtime'
runtime.mkdir(exist_ok=False)
source_user = repo/'bin'/'x64'/'Release'/'user'
user = runtime/'user'
user.mkdir()
for name in ('nand', 'game_content', 'game_library', 'play_time'):
    shutil.copytree(source_user/name, user/name)
(user/'portable.txt').touch()
(user/'config').mkdir()
config = json.loads((repo/'config'/'NxEmu.config').read_text())
config['Core']['ModuleDirectory-x64'] = str(work/'master'/'modules'/'x64')
config['Core']['LogFilter'] = '*:Info'
config['UI']['PerformVulkanCheck'] = False
config['UI']['EnableDiscordPresence'] = False
config['UI']['GameDirectories'] = [str(Path(r'D:\Emulation\switch\NXDev\NxRoom\decrypted'))]
config.setdefault('nxemu-video', {}).setdefault('video', {})['use_vulkan_driver_pipeline_cache'] = False
config['nxemu-video']['video']['use_disk_shader_cache'] = True
(user/'config'/'NxEmu.config').write_text(json.dumps(config, indent=2))
shader = source_user/'shader'/'0100152000022000'
backup = work/'original-mariokart-cache'
backup.mkdir()
moved = []
for name in ('vulkan.bin', 'vulkan_pipelines.bin'):
    path = shader/name
    if path.exists():
        if not path.resolve().is_relative_to(source_user.resolve()):
            raise ValueError('cache path outside intended NXEmu user directory')
        shutil.move(str(path), backup/name)
        moved.append(dict(file=name, bytes=(backup/name).stat().st_size))
(work/'cache-backup.json').write_text(json.dumps(moved, indent=2))
print(json.dumps(dict(runtime=str(runtime), cache_backup=moved), indent=2))
