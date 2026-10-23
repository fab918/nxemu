import copy
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from nxshader import canonical,sha,parse_cache
from local_cache import legacy_bytes
from portable_profile import assemble


class PortableProfileTests(unittest.TestCase):
    def fixture(self):
        code=b'12345678';h=sha(code)
        env=dict(shader=h,local_memory_size=0,texture_bound=0,guest_start_address=0,guest_read_lowest=0,
            guest_read_highest=0,viewport_transform_state=1,stage=5,texture_types=[],texture_pixel_formats=[],
            cbuf_values=[[0xffffffffffffffff,7]],cbuf_replacements=[],workgroup_size=[1,1,1],shared_memory_size=0)
        eid=sha(canonical(env));key=struct.pack('<QI3I8I2I',0xfedcba9876543210,0,1,1,1,*([0]*8),0,0)
        profile=dict(title_id='0100152000022000',content_label='synthetic',
            shaders={h:dict(size=8,references=[dict(encoding='raw',file='game.dat',offset=0,size=8)])},
            environments={eid:env},pipelines=[dict(kind='compute',key_le_hex=key.hex(),environments=[eid])])
        return assemble(profile,[],None,0)[0]

    def test_exact_u64_encoding_and_no_host_key(self):
        p=self.fixture();self.assertEqual(p['recipes'][0]['shader_hash'],str(0xfedcba9876543210))
        self.assertNotIn('key_le_hex',canonical(p).decode())
        self.assertNotIn('feature_bits',canonical(p).decode())
        for eid,env in p['environments'].items():self.assertEqual(eid,sha(canonical(env)))

    def test_local_frame_integrity_and_tail(self):
        def frame(kind,data):return bytes([kind])+struct.pack('<I',len(data))+hashlib.sha256(bytes([kind])+data).digest()+data
        header=b'NXSCACHE'+struct.pack('<II',1,20)
        self.assertEqual(legacy_bytes(header+b'tail'),b'yuzucach'+struct.pack('<I',20))
        data=header+frame(1,b'12345678');self.assertEqual(legacy_bytes(data),b'yuzucach'+struct.pack('<I',20))
        with self.assertRaises(ValueError):legacy_bytes(data[:-1]+b'x')

    def native(self,mutate=None,game=b'12345678',title='0100152000022000'):
        binary=os.environ.get('NXSHADER_READER')
        if not binary:self.skipTest('set NXSHADER_READER to exercise the native reader')
        with tempfile.TemporaryDirectory(prefix='nxshader-test-',dir=Path.cwd()) as directory:
            root=Path(directory);p=self.fixture()
            if mutate:mutate(p)
            (root/'game.dat').write_bytes(game);(root/'test.nxshader').write_bytes(canonical(p))
            result=subprocess.run([binary,str(root/'test.nxshader'),str(root),title,str(root/'out.nxc'),'63'],capture_output=True)
            cache=(root/'out.nxc').read_bytes() if (root/'out.nxc').exists() else None
            return result,cache

    def test_native_reconstructs_exact_program_and_hash(self):
        result,data=self.native();self.assertEqual(result.returncode,0,result.stderr)
        records=parse_cache(legacy_bytes(data));self.assertEqual(len(records),1)
        self.assertEqual(records[0][0][0][1],b'12345678')
        self.assertEqual(struct.unpack_from('<Q',records[0][1])[0],0xfedcba9876543210)

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


if __name__=='__main__':unittest.main()
