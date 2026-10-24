"""Enrich an existing POC profile using exact matches in Yaz0 resources."""
import argparse
import collections
import json
import mmap
from pathlib import Path
import re
import time
from game_resources import resources, decode_yaz0
from nxshader import export_profile, load_profile, safe_file, write_profile


def scan(profile, codes, root, files, library=None):
    unresolved = {h:c for h,c in codes.items() if not profile['shaders'][h]['references']}
    prefixes = collections.defaultdict(list)
    for digest, code in unresolved.items():
        prefixes[code[:128]].append(digest)
    pattern = re.compile(b'|'.join(re.escape(x) for x in sorted({c[:16] for c in unresolved.values()})))
    report = []
    for relative in files:
        started = time.monotonic()
        decoded_bytes = containers = matches = 0
        with safe_file(root, relative).open('rb') as f, mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as data:
            entries = list(resources(data))
            for entry in entries:
                off, size = entry['offset'], entry['size']
                if data[off:off+4] != b'Yaz0':
                    continue
                decoded = decode_yaz0(data[off:off+size], library)
                containers += 1
                decoded_bytes += len(decoded)
                for match in pattern.finditer(decoded):
                    pos = match.start()
                    for digest in prefixes.get(decoded[pos:pos+128], []):
                        code = unresolved[digest]
                        if profile['shaders'][digest]['references'] or decoded[pos:pos+len(code)] != code:
                            continue
                        profile['shaders'][digest]['references'].append(dict(
                            file=relative, encoding='yaz0', container_offset=off, container_size=size,
                            offset=pos, size=len(code), resource=entry['name'],
                            offset_space='decoded-resource'))
                        matches += 1
                if containers % 40 == 0:
                    print(json.dumps(dict(containers=containers, decoded_bytes=decoded_bytes, matches=matches)), flush=True)
        report.append(dict(file=relative, containers=containers, decoded_bytes=decoded_bytes,
                           new_exact_shaders=matches, seconds=round(time.monotonic()-started, 3)))
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cache')
    parser.add_argument('profile')
    parser.add_argument('game_root')
    parser.add_argument('output')
    parser.add_argument('files', nargs='+')
    parser.add_argument('--yaz0-library')
    args = parser.parse_args()
    profile = load_profile(args.profile)
    _, codes, _ = export_profile(args.cache, profile['title_id'], profile['content_label'])
    report = scan(profile, codes, args.game_root, args.files, args.yaz0_library)
    sizes = write_profile(args.output, profile)
    Path(args.output+'.report.json').write_text(json.dumps(dict(files=report, sizes=sizes), indent=2))
    print(json.dumps(report, indent=2))
