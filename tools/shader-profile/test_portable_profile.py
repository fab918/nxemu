import copy
import hashlib
import json
import os
import random
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch
from nxshader import canonical,sha,parse_cache
from local_cache import legacy_bytes
from portable_profile import assemble


def native_program_hash(code):
    import ctypes
    binary=os.environ.get('NXSHADER_READER')
    if not binary:return 0xfedcba9876543210
    directory=Path(binary).parent
    library=next(directory/name for name in ('yaz0-scan.dll','libyaz0-scan.dylib','libyaz0-scan.so') if (directory/name).is_file())
    function=ctypes.CDLL(str(library)).nxshader_hash64
    function.argtypes=[ctypes.c_char_p,ctypes.c_size_t];function.restype=ctypes.c_uint64
    return function(bytes(code),len(code))


class PortableProfileTests(unittest.TestCase):
    def test_export_timings_are_opt_in_and_preserve_results(self):
        from portable_profile import ExportTimings
        disabled=ExportTimings()
        self.assertEqual(disabled.call('stage',lambda:7),7)
        disabled.add('bytes',5)
        self.assertEqual(disabled.report()['phases'],{})
        self.assertEqual(disabled.report()['counts'],{})
        enabled=ExportTimings(True)
        with self.assertRaises(ValueError):
            enabled.call('failed',lambda:(_ for _ in ()).throw(ValueError()))
        self.assertEqual(enabled.report()['phases']['failed']['calls'],1)

    def test_native_timings_preserve_cache_bytes(self):
        with patch.dict(os.environ,{'NXEMU_NXSHADER_TIMING':'0'}):
            plain,plain_cache=self.native()
        with patch.dict(os.environ,{'NXEMU_NXSHADER_TIMING':'1'}):
            measured,measured_cache=self.native()
        self.assertEqual(plain.returncode,0,plain.stderr)
        self.assertEqual(measured.returncode,0,measured.stderr)
        self.assertEqual(plain_cache,measured_cache)
        self.assertNotIn('timings',json.loads(plain.stdout))
        timings=json.loads(measured.stdout)['timings']
        self.assertEqual(timings['counts']['programs'],1)
        self.assertEqual(timings['counts']['resource_read_bytes'],8)
        self.assertEqual(timings['phases']['cache_append']['completed'],1)
        self.assertGreaterEqual(timings['phases']['reader_total']['ms'],0)

    def test_legacy_join_ignores_only_inactive_depth_format(self):
        from rebuild_recipes import normalized
        inactive = bytearray(1400)
        struct.pack_into('<I', inactive, 420, (20 << 6) | (1 << 17))
        expected = bytearray(inactive)
        struct.pack_into('<I', expected, 420, 1 << 17)
        self.assertEqual(normalized(inactive, {}), bytes(expected))
        # Keep active formats and HLE identity: only the unused format is redundant.
        active = bytearray(inactive)
        struct.pack_into('<I', active, 420, (20 << 6) | (1 << 17) | (1 << 5))
        self.assertEqual(normalized(active, {}), bytes(active))

    def fixture(self):
        code=b'12345678';h=sha(code)
        env=dict(shader=h,local_memory_size=0,texture_bound=0,guest_start_address=0,guest_read_lowest=0,
            guest_read_highest=0,viewport_transform_state=1,stage=5,texture_types=[],texture_pixel_formats=[],
            cbuf_values=[[0xffffffffffffffff,7]],cbuf_replacements=[],workgroup_size=[1,1,1],shared_memory_size=0)
        eid=sha(canonical(env));key=struct.pack('<QI3I8I2I',native_program_hash(code),0,1,1,1,*([0]*8),7,0)
        profile=dict(title_id='0100152000022000',content_label='synthetic',
            shaders={h:dict(size=8,references=[dict(encoding='raw',file='game.dat',offset=0,size=8)])},
            environments={eid:env},pipelines=[dict(kind='compute',key_le_hex=key.hex(),environments=[eid])])
        return assemble(profile,[],None,0)[0]

    def test_exact_u64_encoding_and_no_host_key(self):
        p=self.fixture();p['recipes'][0]['shader_hash']=str(0xfedcba9876543210);self.assertEqual(p['recipes'][0]['shader_hash'],str(0xfedcba9876543210))
        self.assertNotIn('key_le_hex',canonical(p).decode())
        self.assertNotIn('feature_bits',canonical(p).decode())
        for eid,env in p['environments'].items():self.assertEqual(eid,sha(canonical(env)))

    def test_local_frame_integrity_and_tail(self):
        def frame(kind,data):return bytes([kind])+struct.pack('<I',len(data))+hashlib.sha256(bytes([kind])+data).digest()+data
        header=b'NXSCACHE'+struct.pack('<II',1,20)
        self.assertEqual(legacy_bytes(header+b'tail'),b'yuzucach'+struct.pack('<I',20))
        data=header+frame(1,b'12345678');self.assertEqual(legacy_bytes(data),b'yuzucach'+struct.pack('<I',20))
        with self.assertRaises(ValueError):legacy_bytes(data[:-1]+b'x')

    def native(self,mutate=None,game=b'12345678',title='0100152000022000',compressed=False,corrupt=False):
        binary=os.environ.get('NXSHADER_READER')
        if not binary:self.skipTest('set NXSHADER_READER to exercise the native reader')
        with tempfile.TemporaryDirectory(prefix='nxshader-test-',dir=Path.cwd()) as directory:
            root=Path(directory);p=self.fixture()
            if mutate:mutate(p)
            (root/'game.dat').write_bytes(game);(root/'test.nxshader').write_bytes(canonical(p))
            if compressed:
                exporter=Path(binary).with_name('nxshader-export'+Path(binary).suffix)
                subprocess.run([str(exporter),'--compact',str(root/'test.nxshader'),str(root/'packed.nxshader')],check=True,capture_output=True)
                payload=(root/'packed.nxshader').read_bytes()
                if corrupt:payload=payload[:-1]+bytes([payload[-1]^1])
                (root/'test.nxshader').write_bytes(payload)
            result=subprocess.run([binary,str(root/'test.nxshader'),str(root),title,str(root/'out.nxc'),'63'],capture_output=True)
            cache=(root/'out.nxc').read_bytes() if (root/'out.nxc').exists() else None
            return result,cache

    def test_native_reconstructs_exact_program_and_hash(self):
        result,data=self.native();self.assertEqual(result.returncode,0,result.stderr)
        records=parse_cache(legacy_bytes(data));self.assertEqual(len(records),1)
        self.assertEqual(records[0][0][0][1],b'12345678')
        self.assertEqual(struct.unpack_from('<Q',records[0][1])[0],native_program_hash(b'12345678'))

    def test_native_rejects_wrong_title(self):
        result,data=self.native(title='0100000000000000');self.assertNotEqual(result.returncode,0);self.assertIsNone(data)

    def test_native_rejects_game_mismatch(self):
        result,data=self.native(game=b'12345679');self.assertNotEqual(result.returncode,0);self.assertIsNone(data)

    def test_native_rejects_payload_field(self):
        result,data=self.native(lambda p:p.update(shader_code='payload'))
        self.assertNotEqual(result.returncode,0);self.assertIsNone(data)

    def test_native_rejects_path_traversal(self):
        def mutate(p):next(iter(p['shaders'].values()))['references'][0]['file']='../game.dat'
        result,data=self.native(mutate);self.assertNotEqual(result.returncode,0);self.assertIsNone(data)

    def test_native_accepts_compressed_profile(self):
        result,data=self.native(compressed=True);self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(len(parse_cache(legacy_bytes(data))),1)

    def test_native_rejects_compressed_checksum_mismatch(self):
        result,data=self.native(compressed=True,corrupt=True)
        self.assertNotEqual(result.returncode,0);self.assertIsNone(data)

    def test_native_yaz0_overlapping_backreference(self):
        code=b'12341234';digest=sha(code)
        raw=b'Yaz0'+struct.pack('>I',8)+bytes(8)+b'\xf01234\x20\x03'
        def mutate(p):
            entry=next(iter(p['shaders'].values()))
            entry['references']=[dict(encoding='yaz0',file='game.dat',container_offset=0,
                container_size=len(raw),offset=0,size=8)]
            p['shaders']={digest:entry}
            next(iter(p['environments'].values()))['shader']=digest
            p['recipes'][0]['shader_hash']=str(native_program_hash(code))
        result,data=self.native(mutate,game=raw);self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(parse_cache(legacy_bytes(data))[0][0][0][1],code)

    def test_native_lz4_distance_one_and_invalid_distance(self):
        code=b'A'*8;digest=sha(code);raw=b'\x13A\x01\x00'
        def mutate(p):
            entry=next(iter(p['shaders'].values()))
            entry['references']=[dict(encoding='nso-lz4',file='game.dat',container_offset=0,
                container_size=len(raw),offset=0,size=8,decoded_size=8,
                container_sha256=sha(raw),segment_sha256=digest)]
            p['shaders']={digest:entry}
            next(iter(p['environments'].values()))['shader']=digest
            p['recipes'][0]['shader_hash']=str(native_program_hash(code))
        result,data=self.native(mutate,game=raw)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(parse_cache(legacy_bytes(data))[0][0][0][1],code)
        result,data=self.native(mutate,game=b'\x13A\x00\x00')
        self.assertNotEqual(result.returncode,0);self.assertIsNone(data)

    def test_native_verified_yaz0_prefix_and_unused_tail_integrity(self):
        code=b'12345678';digest=sha(code)
        raw=b'Yaz0'+struct.pack('>I',16)+bytes(8)+b'\xff'+code+b'\xffABCDEFGH'
        def mutate(p):
            entry=next(iter(p['shaders'].values()))
            entry['references']=[dict(encoding='bktr-yaz0',offset=0,size=8,
                physical_spans=[dict(file='game.dat',offset=0,size=len(raw))],
                resource_size=len(raw),resource_sha256=sha(raw))]
            p['shaders']={digest:entry}
            next(iter(p['environments'].values()))['shader']=digest
            p['recipes'][0]['shader_hash']=str(native_program_hash(code))
        with patch.dict(os.environ,{'NXEMU_NXSHADER_TIMING':'1'}):
            result,data=self.native(mutate,game=raw)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(parse_cache(legacy_bytes(data))[0][0][0][1],code)
        self.assertEqual(json.loads(result.stdout)['timings']['counts']['resource_decoded_bytes'],8)
        result,data=self.native(mutate,game=raw[:-1]+b'X')
        self.assertNotEqual(result.returncode,0);self.assertIsNone(data)

    def test_native_yaz0_prefix_ending_inside_match(self):
        code=b'ABCDABCD';digest=sha(code)
        raw=b'Yaz0'+struct.pack('>I',12)+bytes(8)+b'\xf0ABCD\x60\x03'
        def mutate(p):
            entry=next(iter(p['shaders'].values()))
            entry['references']=[dict(encoding='bktr-yaz0',offset=0,size=8,
                physical_spans=[dict(file='game.dat',offset=0,size=len(raw))],
                resource_size=len(raw),resource_sha256=sha(raw))]
            p['shaders']={digest:entry}
            next(iter(p['environments'].values()))['shader']=digest
            p['recipes'][0]['shader_hash']=str(native_program_hash(code))
        result,data=self.native(mutate,game=raw)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(parse_cache(legacy_bytes(data))[0][0][0][1],code)

    def test_accelerated_yaz0_matches_generated_reference(self):
        binary=os.environ.get('NXSHADER_READER')
        if not binary:self.skipTest('native tools unavailable')
        from game_resources import decode_yaz0
        directory=Path(binary).parent
        library=next((directory/name for name in ('yaz0-scan.dll','libyaz0-scan.dylib','libyaz0-scan.so') if (directory/name).is_file()),None)
        if library is None:self.skipTest('native Yaz0 library unavailable')
        for seed in range(16):
            rng=random.Random(seed);expected=bytearray();tokens=[]
            while len(expected)<16384:
                remaining=16384-len(expected)
                if not expected or remaining<3 or rng.random()<0.35:
                    value=rng.randrange(256);expected.append(value);tokens.append((True,bytes([value])))
                else:
                    length=rng.randrange(3,min(273,remaining)+1);distance=rng.randrange(1,min(len(expected),4096)+1)
                    encoded=(((length-2)<<12) if length<18 else 0)|(distance-1)
                    token=encoded.to_bytes(2,'big')+(bytes([length-18]) if length>=18 else b'')
                    tokens.append((False,token))
                    for _ in range(length):expected.append(expected[-distance])
            raw=bytearray(b'Yaz0'+struct.pack('>I',len(expected))+bytes(8))
            for index in range(0,len(tokens),8):
                group=tokens[index:index+8];raw.append(sum((128>>i) for i,(literal,_) in enumerate(group) if literal))
                raw.extend(b''.join(data for _,data in group))
            with self.subTest(seed=seed):
                self.assertEqual(decode_yaz0(bytes(raw),library),bytes(expected))
                def mutate(p):
                    digest=sha(expected);entry=next(iter(p['shaders'].values()))
                    entry['size']=len(expected)
                    entry['references']=[dict(encoding='yaz0',file='game.dat',container_offset=0,
                        container_size=len(raw),offset=0,size=len(expected))]
                    p['shaders']={digest:entry}
                    env=next(iter(p['environments'].values()))
                    env['shader']=digest;env['guest_read_highest']=len(expected)-8
                    p['recipes'][0]['shader_hash']=str(native_program_hash(expected))
                result,data=self.native(mutate,game=bytes(raw))
                self.assertEqual(result.returncode,0,result.stderr)
                self.assertEqual(parse_cache(legacy_bytes(data))[0][0][0][1],bytes(expected))


if __name__=='__main__':unittest.main()
