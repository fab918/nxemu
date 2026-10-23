"""Cross-platform nxshader v2 exporter. No pip packages, shader payloads or host pipeline keys.

convert: convert an already located v1 POC, preserving its captured guest recipes.
export: read a local v20/vulkan.nxc cache, guest recipe recording and local game files.
Resource discovery is offline. Only plaintext DNCA/PFS containers are supported by this prototype.
"""
import argparse
import collections
import copy
import ctypes
import json
import mmap
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import time
from contextlib import contextmanager
from nxshader import export_profile,load_profile,canonical,sha,write_new,scan_game,safe_file
from shader_span import without_extra_cache_word,rebind_resource_span


class ExportTimings:
    """Opt-in wall times; discovery subphases are included in resource_discovery."""
    def __init__(self,enabled=False):
        self.enabled=enabled;self.phases={};self.counts=collections.Counter()

    @contextmanager
    def stage(self,name):
        if not self.enabled:
            yield
            return
        started=time.perf_counter()
        try:yield
        finally:
            phase=self.phases.setdefault(name,dict(ms=0,calls=0))
            phase['ms']+=1000*(time.perf_counter()-started);phase['calls']+=1

    def call(self,name,function,*args):
        with self.stage(name):return function(*args)

    def add(self,name,value):
        if self.enabled:self.counts[name]+=value

    def report(self):return dict(schema=1,phases=self.phases,counts=dict(self.counts))


def encode_in_process(data,library,level=3):
    if not Path(library).is_file():return None
    dll=ctypes.CDLL(str(Path(library).resolve()))
    try:bound,pack=dll.nxshader_pack_bound,dll.nxshader_pack
    except AttributeError:return None  # Older Yaz0-only acceleration libraries remain usable.
    bound.argtypes=[ctypes.c_size_t];bound.restype=ctypes.c_size_t
    pack.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_int,ctypes.c_void_p,ctypes.c_size_t,ctypes.POINTER(ctypes.c_size_t)]
    pack.restype=ctypes.c_int
    capacity=bound(len(data))
    if not capacity:raise ValueError('profile size limit')
    output=ctypes.create_string_buffer(capacity);written=ctypes.c_size_t()
    if pack(data,len(data),level,output,capacity,ctypes.byref(written)) or written.value>capacity:
        raise ValueError('native profile encoding failed')
    return output.raw[:written.value]


def projections(recipes,projector,bits):
    if not recipes:return []
    with tempfile.TemporaryDirectory(prefix='nxshader-project-',dir=Path.cwd()) as temporary:
        source,target=Path(temporary)/'in',Path(temporary)/'out'
        data=bytearray(struct.pack('<I',len(recipes)))
        for r in recipes:
            w=r['guest_words'];data+=struct.pack('<I',len(w))+struct.pack(f'<{len(w)}I',*w)
        source.write_bytes(data)
        info=json.loads(subprocess.check_output([str(Path(projector).resolve()),str(source),str(target),str(bits)]))
        if info['count']!=len(recipes) or info['state_bytes']!=984:raise ValueError('unsupported projection ABI')
        data=target.read_bytes()
        if len(data)!=988*len(recipes):raise ValueError('projection output size')
        return [data[i*988+4:i*988+988] for i in range(len(recipes))]


def observed(capture):
    if capture.get('failed') or capture.get('projection_mismatches') or capture.get('dropped_observations'):
        raise ValueError('recipe recording is incomplete or projection failed')
    n=capture['guest_state_word_count'];out=[]
    for record in capture['recipes']:
        w=record['words']
        if len(w)!=n+104:raise ValueError('recipe word count')
        out.append(dict(kind='graphics',guest_words=w[:n],
            shader_hashes=[w[i]|w[i+1]<<32 for i in range(n,n+12,2)],
            cbuf_sizes=w[n+12:n+102],texture_pool=w[n+102:]))
    return out


def assemble(profile,graphics,projector,bits,allow_partial=False):
    # Normalization is used only to associate source observations with their source environments.
    # These source keys and feature bits are never exported.
    from rebuild_recipes import masks,normalized,effective_size
    dependencies=masks(profile);by_key={};compute=[]
    for p in profile['pipelines']:
        if any(not profile['shaders'][profile['environments'][e]['shader']]['references'] for e in p['environments']):
            if allow_partial:continue
            raise ValueError('pipeline has unlocated shader')
        key=bytes.fromhex(p['key_le_hex'])
        if p['kind']=='graphics':
            k=normalized(key,dependencies);by_key[k[:416+effective_size(k)]]=p['environments']
        else:
            v=struct.unpack('<QI3I8I2I',key)
            compute.append(dict(kind='compute',shader_hash=v[0],shared_memory_size=v[1],workgroup_size=list(v[2:5]),
                cbuf_sizes=list(v[5:13]),texture_table_size=v[13],environments=p['environments']))
    recipes=[];unmatched=[]
    for r,state in zip(graphics,projections(graphics,projector,bits)):
        flat=r['cbuf_sizes']
        if len(flat)==5 and isinstance(flat[0],list):flat=sum(flat,[])
        key=struct.pack('<6Q90I2I',*r['shader_hashes'],*flat,*r['texture_pool'])+state
        key=normalized(key,dependencies);environments=by_key.get(key[:416+effective_size(key)])
        if environments is None:
            unmatched.append(dict(kind='graphics',guest_words=r['guest_words'],shader_hashes=r['shader_hashes'],
                cbuf_sizes=[flat[i*18:(i+1)*18] for i in range(5)],texture_pool=r['texture_pool'],
                reason='no_serialized_environment_for_source_key'))
            continue
        recipes.append(dict(kind='graphics',guest_words=r['guest_words'],shader_hashes=r['shader_hashes'],
            cbuf_sizes=[flat[i*18:(i+1)*18] for i in range(5)],texture_pool=r['texture_pool'],environments=environments))
    # Some observations can be skipped draws or HLE aliases with no serialized record. Report them explicitly.
    recipes+=compute
    if not recipes:raise ValueError('no reconstructible recipes')
    unique={canonical(r):r for r in recipes};recipes=list(unique.values())
    env_ids={e for r in recipes for e in r['environments']}
    envs={e:profile['environments'][e] for e in sorted(env_ids)}
    shaders={e['shader']:copy.deepcopy(profile['shaders'][e['shader']]) for e in envs.values()}
    allowed={'encoding','file','offset','size','container_offset','container_size','decoded_size',
             'container_sha256','segment_sha256','physical_spans','resource_size','resource_sha256'}
    for shader in shaders.values():
        ref=shader['references'][0]
        shader['references']=[{k:v for k,v in ref.items() if k in allowed}]
        # Avoid unrelated resource metadata in a raw physical locator.
        if ref['encoding']=='raw':shader['references']=[{k:ref[k] for k in ('encoding','file','offset','size')}]
        elif ref['encoding']=='yaz0':shader['references']=[{k:ref[k] for k in
            ('encoding','file','offset','size','container_offset','container_size')}]
    result=dict(format='nxshader',version=2,title_id=profile['title_id'],guest_state_version=1,
        content_label=profile['content_label'],shaders=shaders,environments=envs,recipes=recipes,
        unresolved_recipes=unmatched)
    def exact_integers(value):
        if isinstance(value,int) and value>0xffffffff:return str(value)
        if isinstance(value,list):return [exact_integers(v) for v in value]
        if isinstance(value,dict):return {k:exact_integers(v) for k,v in value.items()}
        return value
    result=exact_integers(result)
    remap={key:sha(canonical(env)) for key,env in result['environments'].items()}
    result['environments']={remap[key]:env for key,env in result['environments'].items()}
    for recipe in result['recipes']:recipe['environments']=[remap[e] for e in recipe['environments']]
    return result,dict(graphics_observations=len(graphics),unmatched_observations=len(unmatched),
        graphics_recipes=sum(r['kind']=='graphics' for r in recipes),compute_recipes=len(compute),
        shaders=len(shaders),environments=len(envs),source_feature_bits_not_exported=bits)


def discover(profile,codes,root,base,update,library=None,timings=None):
    timings=timings or ExportTimings()
    from game_resources import resources,decode_yaz0
    from nso_resources import decode_lz4
    from bktr_resources import LogicalGames,romfs_files
    # Try full ranges first, then only ranges with a proven terminal followed by one extra word.
    candidates=collections.defaultdict(list)
    for h,code in codes.items():
        candidates[code[:16]].append((h,code,False))
        try:candidates[code[:16]].append((h,without_extra_cache_word(code),True))
        except ValueError:pass
    if not candidates:return
    pattern=re.compile(b'|'.join(re.escape(x) for x in sorted(candidates)))
    # Temporary POC harness; NXEmu can call the same C++ index without Python.
    from native_search import NativeSearch
    native_search=NativeSearch(library,candidates)
    long_prefixes=collections.defaultdict(list);short_prefixes=collections.defaultdict(list)
    for entries in candidates.values():
        for item in entries:
            code=item[1]
            (long_prefixes if len(code)>=128 else short_prefixes)[code[:128]].append(item)
    short_sizes=sorted({len(prefix) for prefix in short_prefixes})
    def locate_impl(data,reference):
        positions=(native_search.positions(data) if native_search.handle else
                   (hit.start() for hit in pattern.finditer(data)))
        for pos in positions:
            possible=long_prefixes.get(data[pos:pos+128],[])
            for size in short_sizes:possible=possible+short_prefixes.get(data[pos:pos+size],[])
            for h,code,trim in possible:
                if h not in profile['shaders'] or profile['shaders'][h]['references']:continue
                if data[pos:pos+len(code)]!=code:continue
                ref=dict(reference(pos),size=len(code))
                if trim:rebind_resource_span(profile,h,codes[h],ref)
                else:profile['shaders'][h]['references']=[ref]
    def locate(data,reference):
        timings.add('search_bytes',len(data))
        return timings.call('discovery_search',locate_impl,data,reference)
    for name in [base]+([update] if update else []):
        with safe_file(root,name).open('rb') as file,mmap.mmap(file.fileno(),0,access=mmap.ACCESS_READ) as data:
            locate(data,lambda pos:dict(file=name,encoding='raw',offset=pos))
            if all(s['references'] for s in profile['shaders'].values()):return
            for entry in resources(data):
                offset,size=entry['offset'],entry['size']
                # With an update, only the logical patched RomFS is authoritative. Do not
                # decode obsolete physical base resources that the patch replaces.
                if not update and entry['kind']=='romfs' and data[offset:offset+4]==b'Yaz0':
                    if size>512*1024*1024:raise ValueError('RomFS resource limit')
                    decoded=timings.call('discovery_decompression',decode_yaz0,data[offset:offset+size],library)
                    locator=dict(file=name,encoding='yaz0',container_offset=offset,container_size=size)
                    locate(decoded,lambda pos:dict(locator,offset=pos))
                    if all(s['references'] for s in profile['shaders'].values()):return
                if entry['kind']!='exefs' or data[offset:offset+4]!=b'NSO0':continue
                flags,=struct.unpack_from('<I',data,offset+12)
                if flags&128:raise ValueError('ZBIC NSO segments are not supported by this exporter')
                for segment in range(3):
                    source,_,decoded_size=struct.unpack_from('<III',data,offset+16+segment*16)
                    compressed=bool(flags&(1<<segment))
                    stored=struct.unpack_from('<I',data,offset+96+segment*4)[0] if compressed else decoded_size
                    if source>size or stored>size-source or decoded_size>512*1024*1024:raise ValueError('NSO bounds')
                    raw=data[offset+source:offset+source+stored]
                    decoded=timings.call('discovery_decompression',decode_lz4,raw,decoded_size) if compressed else raw
                    if flags&(1<<(segment+3)) and timings.call('discovery_verification',sha,decoded)!=data[offset+160+segment*32:offset+192+segment*32].hex():
                        raise ValueError('NSO segment fingerprint')
                    locator=dict(file=name,encoding='nso-lz4',container_offset=offset+source,
                        container_size=stored,decoded_size=decoded_size,
                        container_sha256=timings.call('discovery_verification',sha,raw),
                        segment_sha256=timings.call('discovery_verification',sha,decoded))
                    if compressed:locate(decoded,lambda pos:dict(locator,offset=pos))
                    if all(s['references'] for s in profile['shaders'].values()):return
    if update:
        with LogicalGames(root) as games:
            for _,_,storage in games.pairs(base,update):
                for index,entry in enumerate(romfs_files(storage)):
                    if entry['size']>512*1024*1024:raise ValueError('RomFS resource limit')
                    raw=timings.call('discovery_resource_io',storage.read,entry['offset'],entry['size'])
                    timings.add('resource_files',1);timings.add('resource_read_bytes',len(raw))
                    compressed=raw[:4]==b'Yaz0'
                    decoded=timings.call('discovery_decompression',decode_yaz0,raw,library) if compressed else raw
                    timings.add('resource_decoded_bytes',len(decoded))
                    locator=dict(encoding='bktr-yaz0' if compressed else 'bktr-raw',resource_size=len(raw),
                        resource_sha256=timings.call('discovery_verification',sha,raw),physical_spans=storage.spans(entry['offset'],entry['size']))
                    locate(decoded,lambda pos:dict(locator,offset=pos))
                    if all(s['references'] for s in profile['shaders'].values()):return
                    if index%100==0:print(json.dumps(dict(romfs_files=index,unlocated=sum(not s['references'] for s in profile['shaders'].values()))),flush=True)


def main():
    started=time.perf_counter()
    parser=argparse.ArgumentParser(description=__doc__);sub=parser.add_subparsers(dest='command',required=True)
    convert=sub.add_parser('convert');convert.add_argument('profile');convert.add_argument('output')
    convert.add_argument('--projector',required=True)
    export=sub.add_parser('export');export.add_argument('--cache',required=True);export.add_argument('--recipes',required=True)
    export.add_argument('--game-root',required=True);export.add_argument('--base',required=True)
    export.add_argument('--update');export.add_argument('--output',required=True);export.add_argument('--title-id',required=True)
    export.add_argument('--content-label',required=True);export.add_argument('--projector',required=True)
    export.add_argument('--yaz0-library');export.add_argument('--allow-partial',action='store_true')
    for command in (convert,export):
        command.add_argument('--timings',action='store_true',help='include detailed phase wall times in the report')
        command.add_argument('--codec',help='nxshader-codec executable; by default next to the projector')
        command.add_argument('--plain',action='store_true',help='write inspectable uncompressed JSON')
        command.add_argument('--compression-level',type=int,choices=[1,3,6,9,12,19],default=3)
    args=parser.parse_args()
    timings=ExportTimings(args.timings)
    if args.command=='convert':
        profile=timings.call('source_profile_read',load_profile,args.profile)
        extra=profile['guest_recipe_probe'];bits=extra['target_feature_bits_for_this_test']
        graphics=extra.get('observed_graphics_recipes') or [r for r in extra['recipes'] if r['kind']=='graphics']
    else:
        profile,codes,_=timings.call('cache_read',export_profile,args.cache,args.title_id,args.content_label)
        with timings.stage('recipe_recording_read'):
            capture=json.loads(Path(args.recipes).read_bytes());graphics=observed(capture)
        bits=capture.get('feature_bits')
        if bits is None:raise ValueError('recording lacks source feature bits; use this branch to record')
        timings.call('resource_discovery',discover,profile,codes,args.game_root,args.base,args.update,args.yaz0_library,timings)
    result,report=timings.call('recipe_assembly_and_projection',assemble,profile,graphics,args.projector,bits,getattr(args,'allow_partial',False))
    data=timings.call('json_serialization',canonical,result)
    encoding_started=time.perf_counter()
    if args.plain:timings.call('profile_write',write_new,args.output,data)
    else:
        projector=Path(args.projector).resolve()
        codec=Path(args.codec).resolve() if args.codec else projector.with_name('nxshader-codec'+projector.suffix)
        libraries=[projector.with_name(name) for name in ('yaz0-scan.dll','libyaz0-scan.dylib','libyaz0-scan.so')]
        native=next((path for path in libraries if path.is_file()),None)
        encoded=timings.call('compression_and_library_load',encode_in_process,data,native,args.compression_level) if native and not args.codec else None
        if encoded is not None:timings.call('profile_write',write_new,args.output,encoded)
        else:
            if not codec.is_file():raise ValueError('build nxshader-codec beside the projector, provide --codec, or use --plain')
            with tempfile.TemporaryDirectory(prefix='nxshader-encode-',dir=Path.cwd()) as temporary:
                source=Path(temporary)/'profile.json';source.write_bytes(data)
                with timings.stage('codec_subprocess_and_write'):
                    subprocess.run([str(codec),'pack',str(source),str(Path(args.output).resolve()),str(args.compression_level)],check=True)
    encoded=timings.call('output_verification_read',Path(args.output).read_bytes)
    digest=timings.call('output_verification_hash',sha,encoded)
    report.update(bytes=len(encoded),json_bytes=len(data),sha256=digest,format_version=2,
        encoding='json' if args.plain else 'zstd',compression_level=None if args.plain else args.compression_level,
        encoding_and_write_ms=1000*(time.perf_counter()-encoding_started),export_seconds=time.perf_counter()-started)
    if args.timings:report['timings']=timings.report()
    write_new(str(args.output)+'.report.json',json.dumps(report,indent=2).encode())
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
