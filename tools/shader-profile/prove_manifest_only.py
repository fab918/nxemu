"""Import proof in a process which forbids opening the source ZIP/cache path."""
import json
from pathlib import Path
import sys
import time
from nxshader import load_profile, reconstruct

repo = Path(__file__).resolve().parents[2]
work = repo/'build'/'shader-profile-poc'


def audit(event, args):
    if event == 'open':
        path = str(args[0]).lower().replace('\\', '/')
        if path.endswith('mariokartmacvulkan.bin.zip') or '/original-mariokart-cache/' in path:
            raise RuntimeError('source cache access forbidden during import proof')


sys.addaudithook(audit)
started = time.monotonic()
profile = load_profile(work/'MarioKart-cache-macOS.nxshader')
result = reconstruct(profile, r'D:\Emulation\switch\NXDev\NxRoom\decrypted',
                     work/'manifest-only-proof'/'vulkan.bin', True, work/'yaz0.dll')
result.update(source_cache_open_forbidden=True, import_seconds=time.monotonic()-started)
expected = json.loads((work/'integrity.json').read_text())['rebuilt_sha256']
if result['output_sha256'] != expected:
    raise ValueError('manifest-only reconstruction differs from verified original subset')
(work/'manifest-only-proof.json').write_text(json.dumps(result, indent=2))
print(json.dumps({k:v for k,v in result.items() if k != 'missing_pipeline_indices'}, indent=2))
