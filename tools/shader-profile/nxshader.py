#!/usr/bin/env python3
"""Offline NXEmu cache-v20 POC. No shader instructions in exported profiles.

Only the explicitly supported little-endian v20 ABI is accepted. This is a
local reconstruction adapter, not a portable Vulkan pipeline state format.
"""
import argparse
import collections
import gzip
import hashlib
import json
import mmap
import re
import bisect
import time
from pathlib import Path
import struct
import zipfile

MAGIC = b'yuzucach'
VERSION = 20
GRAPHICS_KEY_SIZE = 1400
COMPUTE_KEY_SIZE = 64
FIELDS = ('local_memory_size', 'texture_bound', 'guest_start_address',
          'guest_read_lowest', 'guest_read_highest', 'viewport_transform_state', 'stage')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':')).encode()


class Reader:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def take(self, size):
        if size < 0 or size > len(self.data) - self.pos:
            raise ValueError(f'truncated cache at {self.pos}, requested {size}')
        out = self.data[self.pos:self.pos + size]
        self.pos += size
        return out

    def unpack(self, fmt):
        return struct.unpack('<' + fmt, self.take(struct.calcsize('<' + fmt)))


def read_cache(path):
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as archive:
            info = archive.getinfo('vulkan.bin')
            if info.file_size > 256 * 1024 * 1024:
                raise ValueError('cache exceeds POC size limit')
            return archive.read(info)
    if Path(path).stat().st_size > 256 * 1024 * 1024:
        raise ValueError('cache exceeds POC size limit')
    data = Path(path).read_bytes()
    if data[:8] == b'NXSCACHE':
        from local_cache import legacy_bytes
        return legacy_bytes(data)
    return data


def parse_cache(data):
    r = Reader(data)
    if r.take(8) != MAGIC or r.unpack('I')[0] != VERSION:
        raise ValueError('unsupported cache magic/version (requires v20)')
    records = []
    while r.pos < len(data):
        begin = r.pos
        count, = r.unpack('I')
        if not 1 <= count <= 6:
            raise ValueError(f'invalid stage count at {begin}')
        envs = []
        for _ in range(count):
            size, nt, nf, nc, nr = r.unpack('5Q')
            meta = dict(zip(FIELDS, r.unpack('7I')))
            stage = meta['stage']
            if stage > 6 or size == 0 or size % 8 or size > 1024 * 1024:
                raise ValueError(f'invalid environment at {r.pos}')
            if meta['guest_read_highest'] - meta['guest_read_lowest'] + 8 != size:
                raise ValueError('inconsistent cached instruction bounds')
            code = r.take(size)
            for name, amount, fmt in [('texture_types', nt, 'II'),
                                       ('texture_pixel_formats', nf, 'II'),
                                       ('cbuf_values', nc, 'QI'),
                                       ('cbuf_replacements', nr, 'QI')]:
                if amount > 65536:
                    raise ValueError('unreasonable environment map size')
                meta[name] = [list(r.unpack(fmt)) for _ in range(amount)]
            header = None
            if stage == 5:
                *workgroup, shared = r.unpack('4I')
                meta.update(workgroup_size=workgroup, shared_memory_size=shared)
            else:
                header = r.take(80)
                meta['program_header_sha256'] = sha(header)
                # Header is recovered from local game bytes, never exported.
                if code[:80] != header:
                    raise ValueError('separate program header differs from code prefix; unsupported')
                if stage == 3:
                    meta['gp_passthrough_mask'] = list(r.unpack('8I'))
            envs.append((meta, code))
        compute = envs[0][0]['stage'] == 5
        if compute and count != 1:
            raise ValueError('invalid compute stage count')
        key = r.take(COMPUTE_KEY_SIZE if compute else GRAPHICS_KEY_SIZE)
        if not compute:
            hashes = struct.unpack_from('<6Q', key)
            if sum(x != 0 for x in hashes) != count:
                raise ValueError('graphics key/environment count mismatch (unsupported ABI)')
        records.append((envs, key, compute, (begin, r.pos)))
    return records


def export_profile(cache, title_id, content_label):
    data = read_cache(cache)
    records = parse_cache(data)
    profile = dict(format='nxshader-poc', version=1, cache_version=VERSION,
                   abi='nxemu-v20-le-gfx1400-compute64', title_id=title_id,
                   content_label=content_label, source_cache_sha256=sha(data),
                   shaders={}, environments={}, pipelines=[])
    codes, keys, states, combinations = {}, set(), set(), set()
    flags, stages = collections.Counter(), collections.Counter()
    code_bytes = 0
    for envs, key, compute, _ in records:
        env_ids = []
        for meta, code in envs:
            digest = sha(code)
            codes[digest] = code
            profile['shaders'].setdefault(digest, dict(size=len(code), references=[]))
            entry = dict(meta, shader=digest)
            env_id = sha(canonical(entry))
            profile['environments'][env_id] = entry
            env_ids.append(env_id)
            code_bytes += len(code)
            stages[meta['stage']] += 1
        # Raw key contains hashes, sizes and packed fixed state, no instructions.
        signature = (compute, key, tuple(env_ids))
        if signature in keys:
            continue
        keys.add(signature)
        profile['pipelines'].append(dict(kind='compute' if compute else 'graphics',
                                         key_le_hex=key.hex(), environments=env_ids))
        if not compute:
            raw1, raw2 = struct.unpack_from('<II', key, 416)
            flags[f'{raw1 & 63:02x}:{(raw2 >> 20) & 1}'] += 1
            states.add(key[416:])
            combinations.add(key[:48])
    stats = dict(cache_bytes=len(data), cache_sha256=sha(data), records=len(records),
                 pipelines=len(profile['pipelines']), duplicate_records=len(records)-len(keys),
                 unique_shaders=len(codes), unique_environments=len(profile['environments']),
                 serialized_code_bytes=code_bytes, unique_code_bytes=sum(map(len, codes.values())),
                 graphics_feature_signatures=dict(flags), stage_occurrences=dict(stages),
                 unique_fixed_states=len(states), unique_shader_combinations=len(combinations))
    return profile, codes, stats


def write_profile(path, profile):
    raw = canonical(profile)
    write_new(path, gzip.compress(raw, compresslevel=9, mtime=0))
    return len(raw), Path(path).stat().st_size


def write_new(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('xb') as f:
        f.write(data)


def load_profile(path):
    with gzip.open(path, 'rb') as f:
        raw = f.read(64 * 1024 * 1024 + 1)
    if len(raw) > 64 * 1024 * 1024:
        raise ValueError('profile exceeds POC size limit')
    p = json.loads(raw)
    if (p.get('format'), p.get('version'), p.get('cache_version'), p.get('abi')) != (
            'nxshader-poc', 1, VERSION, 'nxemu-v20-le-gfx1400-compute64'):
        raise ValueError('unsupported profile schema/ABI')
    for digest, env in p['environments'].items():
        if sha(canonical(env)) != digest:
            raise ValueError('environment digest mismatch')
    return p


def safe_file(root, relative):
    root = Path(root).resolve()
    path = (root / relative).resolve()
    if not path.is_relative_to(root) or not path.is_file():
        raise ValueError('reference outside game root or not a regular file')
    return path


def resolve_code(profile, root, yaz0_library=None):
    from game_resources import decode_yaz0
    codes = {}
    compressed = collections.defaultdict(list)
    logical = []
    nso = collections.defaultdict(list)
    for digest, shader in profile['shaders'].items():
        if not 0 < shader['size'] <= 1024 * 1024:
            raise ValueError('invalid shader size')
        for ref in shader['references']:
            if ref.get('encoding') == 'nso-lz4':
                nso[(ref['file'],ref['container_offset'],ref['container_size'],ref['decoded_size'])].append((digest,ref))
                continue
            if ref.get('encoding') in ('bktr-raw', 'bktr-yaz0'):
                logical.append((digest, ref))
                continue
            if ref.get('encoding') == 'yaz0':
                compressed[(ref['file'], ref['container_offset'], ref['container_size'])].append((digest, ref))
                continue
            if ref.get('encoding') != 'raw':
                raise ValueError('unsupported reference encoding')
            path = safe_file(root, ref['file'])
            if ref['offset'] < 0 or ref['size'] != shader['size']:
                raise ValueError('invalid shader reference')
            with path.open('rb') as f:
                f.seek(ref['offset'])
                code = f.read(shader['size'])
            if sha(code) == digest:
                codes[digest] = code
                break
    for (relative, offset, size), refs in compressed.items():
        if all(digest in codes for digest, _ in refs):
            continue
        path = safe_file(root, relative)
        if offset < 0 or not 0 < size <= 512 * 1024 * 1024:
            raise ValueError('invalid compressed container bounds')
        with path.open('rb') as f:
            f.seek(offset)
            raw = f.read(size)
        decoded = decode_yaz0(raw, yaz0_library)
        for digest, ref in refs:
            if digest in codes:
                continue
            if ref['offset'] < 0 or ref['size'] != profile['shaders'][digest]['size']:
                raise ValueError('invalid decompressed shader reference')
            code = decoded[ref['offset']:ref['offset']+ref['size']]
            if sha(code) == digest:
                codes[digest] = code
    if logical:
        from bktr_resources import resolve_references
        codes.update(resolve_references(profile, root, logical, yaz0_library))
    if nso:
        from nso_resources import decode_lz4
        for (relative,offset,size,decoded_size),refs in nso.items():
            if all(digest in codes for digest,_ in refs): continue
            if offset<0 or not 0<size<=512*1024*1024: raise ValueError('NSO reference bounds')
            with safe_file(root,relative).open('rb') as f:
                f.seek(offset); raw=f.read(size)
            if any(sha(raw)!=ref['container_sha256'] for _,ref in refs): raise ValueError('NSO container hash mismatch')
            decoded=decode_lz4(raw,decoded_size)
            if any(sha(decoded)!=ref['segment_sha256'] for _,ref in refs): raise ValueError('NSO segment hash mismatch')
            for digest,ref in refs:
                length=profile['shaders'][digest]['size']
                if ref['size']!=length or not 0<=ref['offset']<=len(decoded)-length: raise ValueError('NSO shader reference bounds')
                code=decoded[ref['offset']:ref['offset']+length]
                if sha(code)==digest: codes[digest]=code
    return codes


def scan_game(profile, codes, root, files):
    """Exact full-byte matches only; GPU offsets never used as file offsets."""
    from game_resources import resources
    by_prefix = collections.defaultdict(list)
    for digest, code in codes.items():
        by_prefix[code[:min(128, len(code))]].append(digest)
    anchors = sorted(set(code[:16] for code in codes.values()))
    short_sizes = {len(c) for c in codes.values() if len(c) < 128}
    pattern = re.compile(b'|'.join(re.escape(x) for x in anchors))
    report = []
    for relative in files:
        path = safe_file(root, relative)
        start = time.monotonic()
        with path.open('rb') as f, mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as data:
            indexed = sorted(resources(data), key=lambda e: e['offset'])
            offsets = [e['offset'] for e in indexed]
            candidates, found = 0, set()
            for match in pattern.finditer(data):
                pos = match.start()
                candidates += 1
                # All supplied shaders are at least 128 bytes. Short shaders
                # use their entire byte string as the comparison prefix.
                possible = by_prefix.get(data[pos:pos+128], [])
                for size in short_sizes:
                    possible = possible + by_prefix.get(data[pos:pos+size], [])
                for digest in possible:
                    code = codes[digest]
                    if data[pos:pos+len(code)] != code:
                        continue
                    found.add(digest)
                    refs = profile['shaders'][digest]['references']
                    # One verified physical reference per input file is sufficient.
                    if any(x['file'] == relative for x in refs):
                        continue
                    ref = dict(file=relative, offset=pos, size=len(code), encoding='raw')
                    index = bisect.bisect_right(offsets, pos)-1
                    if index >= 0:
                        entry = indexed[index]
                        if pos+len(code) <= entry['offset']+entry['size']:
                            ref.update(resource=entry['name'], resource_offset=pos-entry['offset'],
                                       resource_kind=entry['kind'])
                    refs.append(ref)
            report.append(dict(file=relative, bytes=len(data), indexed_resources=len(indexed),
                               raw_candidates=candidates, exact_shaders=len(found),
                               seconds=round(time.monotonic()-start, 3)))
        print(json.dumps(report[-1]), flush=True)
    unresolved = [h for h, s in profile['shaders'].items() if not s['references']]
    return dict(files=report, exact_shaders=len(codes)-len(unresolved),
                total_shaders=len(codes), unresolved_shaders=unresolved,
                unresolved_classification='not found in raw bytes; dynamic NOT established')


def env_bytes(env, code):
    maps = [env[k] for k in ('texture_types', 'texture_pixel_formats',
                             'cbuf_values', 'cbuf_replacements')]
    out = struct.pack('<5Q7I', len(code), *map(len, maps), *(env[k] for k in FIELDS)) + code
    for entries, fmt in zip(maps, ('II', 'II', 'QI', 'QI')):
        for entry in entries:
            out += struct.pack('<' + fmt, *entry)
    if env['stage'] == 5:
        out += struct.pack('<4I', *env['workgroup_size'], env['shared_memory_size'])
    else:
        if sha(code[:80]) != env['program_header_sha256']:
            raise ValueError('program header hash mismatch')
        out += code[:80]
        if env['stage'] == 3:
            out += struct.pack('<8I', *env['gp_passthrough_mask'])
    return out


def reconstruct(profile, root, output, allow_partial=False, yaz0_library=None):
    codes = resolve_code(profile, root, yaz0_library)
    out, built, missing = bytearray(MAGIC + struct.pack('<I', VERSION)), 0, []
    for i, pipeline in enumerate(profile['pipelines']):
        envs = [profile['environments'][e] for e in pipeline['environments']]
        if any(e['shader'] not in codes for e in envs):
            missing.append(i)
            continue
        key = bytes.fromhex(pipeline['key_le_hex'])
        expected = COMPUTE_KEY_SIZE if pipeline['kind'] == 'compute' else GRAPHICS_KEY_SIZE
        if len(key) != expected:
            raise ValueError('invalid pipeline key size')
        out += struct.pack('<I', len(envs))
        for env in envs:
            out += env_bytes(env, codes[env['shader']])
        out += key
        built += 1
    # Validate entire output with the strict parser before publishing any file.
    parse_cache(out)
    report = dict(resolved_shaders=len(codes), total_shaders=len(profile['shaders']),
                  rebuilt_pipelines=built, total_pipelines=len(profile['pipelines']),
                  reconstruction_percent=100 * built / max(1, len(profile['pipelines'])),
                  missing_pipeline_indices=missing, output_sha256=sha(out))
    if missing and not allow_partial:
        return report
    write_new(output, out)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    ex = sub.add_parser('export')
    ex.add_argument('cache')
    ex.add_argument('output')
    ex.add_argument('--title-id', required=True)
    ex.add_argument('--content-label', required=True, help='Explicit local game build; not inferred from cache')
    im = sub.add_parser('import')
    im.add_argument('profile')
    im.add_argument('game_root')
    im.add_argument('output')
    im.add_argument('--allow-partial', action='store_true')
    im.add_argument('--yaz0-library', help='Optional locally compiled acceleration library')
    sc = sub.add_parser('scan')
    sc.add_argument('cache')
    sc.add_argument('game_root')
    sc.add_argument('output')
    sc.add_argument('files', nargs='+', help='Container paths relative to game_root')
    sc.add_argument('--title-id', required=True)
    sc.add_argument('--content-label', required=True)
    args = parser.parse_args()
    if args.command == 'export':
        profile, _, stats = export_profile(args.cache, args.title_id, args.content_label)
        stats['json_bytes'], stats['manifest_bytes'] = write_profile(args.output, profile)
        print(json.dumps(stats, indent=2))
    elif args.command == 'scan':
        profile, codes, stats = export_profile(args.cache, args.title_id, args.content_label)
        stats['scan'] = scan_game(profile, codes, args.game_root, args.files)
        stats['json_bytes'], stats['manifest_bytes'] = write_profile(args.output, profile)
        Path(args.output+'.report.json').write_text(json.dumps(stats, indent=2))
        print(json.dumps({k:v for k,v in stats.items() if k != 'scan'}, indent=2))
    else:
        report = reconstruct(load_profile(args.profile), args.game_root, args.output, args.allow_partial, args.yaz0_library)
        print(json.dumps(report, indent=2))
        if report['missing_pipeline_indices'] and not args.allow_partial:
            raise SystemExit(2)


if __name__ == '__main__':
    main()
