"""Reproducible metadata-only workloads; no game data or GPU required.

Run with NXSHADER_READER and NXSHADER_EXPORTER pointing to Release tools.
Output includes exact coverage checks, phase timings and compression tradeoffs.
"""
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import random
import statistics
import struct
import subprocess
import tempfile
import time
from nxshader import canonical
from test_portable_profile import PortableProfileTests, native_program_hash


def run(args):
    started = time.perf_counter()
    result = subprocess.run([str(x) for x in args], capture_output=True,
                            env=dict(os.environ, NXEMU_NXSHADER_TIMING='1'), timeout=120)
    if result.returncode:
        raise RuntimeError(result.stderr.decode(errors='replace'))
    return result.stdout, (time.perf_counter() - started) * 1000


def fixture(root, programs, recipes, code_size):
    profile = PortableProfileTests().fixture()
    template = next(iter(profile['environments'].values()))
    recipe = profile['recipes'][0]
    profile['shaders'] = {}; profile['environments'] = {}; profile['recipes'] = []
    rng = random.Random(918)
    codes = [rng.randbytes(code_size) for _ in range(programs)]
    (root / 'game.dat').write_bytes(b''.join(codes))
    for i, code in enumerate(codes):
        digest = hashlib.sha256(code).hexdigest()
        profile['shaders'][digest] = dict(size=code_size, references=[
            dict(encoding='raw', file='game.dat', offset=i * code_size, size=code_size)])
    hashes = [native_program_hash(code) for code in codes]
    digests = list(profile['shaders'])
    for i in range(recipes):
        env = copy.deepcopy(template)
        env.update(shader=digests[i % programs], guest_read_highest=code_size - 8,
                   shared_memory_size=(i // programs) * 16)
        eid = hashlib.sha256(canonical(env)).hexdigest()
        profile['environments'][eid] = env
        r = copy.deepcopy(recipe)
        r.update(shader_hash=str(hashes[i % programs]), environments=[eid],
                 shared_memory_size=env['shared_memory_size'])
        profile['recipes'].append(r)
    return profile


def benchmark(root, name, programs, recipes, code_size, repeats):
    root.mkdir()
    profile = fixture(root, programs, recipes, code_size)
    source = root / 'profile.json'; source.write_bytes(canonical(profile))
    packed = root / 'profile.nxshader'
    exports = []
    for _ in range(repeats):
        packed.unlink(missing_ok=True)
        exports.append(run([os.environ['NXSHADER_EXPORTER'], '--compact', source, packed])[1])
    samples = []
    for i in range(repeats):
        stdout, elapsed = run([os.environ['NXSHADER_READER'], packed, root,
            profile['title_id'], root / ('cache-%d.nxc' % i), '67'])
        report = json.loads(stdout)
        assert report['recipes'] == recipes, report
        assert report['unique_local_records'] == recipes, report
        assert len(set(report['recipe_ids'])) == recipes, report
        samples.append((elapsed, report))
    sample = sorted(samples, key=lambda x: x[0])[len(samples)//2]
    report = sample[1]
    codec = Path(os.environ['NXSHADER_READER']).with_name('nxshader-codec')
    payload = root / 'payload.bin'
    run([codec, 'unpack', packed, payload])
    stdout, _ = run([codec, 'bench-payload', payload])
    compression = json.loads(stdout)['levels']
    reuse = root / 'reuse.json'; reuse.write_bytes(canonical(report['recipe_ids']))
    # Delete all game bytes: repeated imports must need no resource access.
    (root / 'game.dat').unlink()
    stdout, reuse_ms = run([os.environ['NXSHADER_READER'], packed, root,
        profile['title_id'], root / 'reused.nxc', '67', '--reuse=' + str(reuse)])
    reused = json.loads(stdout)
    assert reused['reused'] == recipes and reused['recipes'] == 0, reused
    assert not (root / 'reused.nxc').exists()
    return dict(name=name, programs=programs, recipes=recipes, code_size=code_size,
        metadata_bytes=source.stat().st_size, nxshader_bytes=packed.stat().st_size,
        payload_bytes=payload.stat().st_size, payload_sha256=hashlib.sha256(payload.read_bytes()).hexdigest(), cache_bytes=report['cache_bytes'],
        nxshader_sha256=hashlib.sha256(packed.read_bytes()).hexdigest(),
        cache_sha256=hashlib.sha256((root / 'cache-0.nxc').read_bytes()).hexdigest(),
        recipe_ids_sha256=hashlib.sha256(canonical(report['recipe_ids'])).hexdigest(),
        export_process_median_ms=statistics.median(exports),
        import_process_median_ms=statistics.median(s[0] for s in samples),
        import_timings=report['timings'], reuse_process_ms=reuse_ms,
        reuse_resource_bytes=reused['timings']['counts']['resource_read_bytes'],
        compression=compression)


def local_cache_benchmark(root, mib, repeats):
    tool=Path(os.environ['NXSHADER_READER']).with_name('cache-store-benchmark')
    path=root / ('local-%d.nxc' % mib)
    stdout,_=run([tool,'generate',path,mib]);generated=json.loads(stdout)
    samples=[]
    for _ in range(repeats):
        stdout,_=run([tool,'read',path]);samples.append(json.loads(stdout))
    assert all(s['records']==mib*4 for s in samples)
    return dict(mib=mib,generated=generated,read_seconds_median=statistics.median(s['seconds'] for s in samples),
        read_peak_resident_bytes=max(s['peak_resident_bytes'] for s in samples),
        file_sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--repeats', type=int, default=3)
    args = parser.parse_args()
    workloads = [('shared-code', 1, 2048, 65536),
                 ('mixed-variants', 32, 2048, 16384),
                 ('unique-programs', 512, 512, 4096),
                 ('large-profile', 4096, 4096, 128)]
    with tempfile.TemporaryDirectory(prefix='nxshader-synthetic-') as temporary:
        results = [benchmark(Path(temporary)/name, name, p, r, c, args.repeats)
                   for name, p, r, c in workloads]
        local=[local_cache_benchmark(Path(temporary),mib,args.repeats) for mib in (16,64)]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(dict(seed=918, repeats=args.repeats,
        note='Synthetic CPU/storage tests; process times include tool startup and verification. No GPU compilation.',
        workloads=results, local_cache=local), indent=2) + '\n')
    print(args.output)


if __name__ == '__main__':
    main()
