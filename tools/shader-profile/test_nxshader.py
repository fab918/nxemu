import json
from pathlib import Path
import struct
import tempfile
import unittest
from game_resources import decode_yaz0
from nxshader import (MAGIC, env_bytes, export_profile, load_profile, parse_cache,
                      reconstruct, sha, write_profile)


class ProfileTests(unittest.TestCase):
    def setUp(self):
        test_root = Path(__file__).resolve().parents[2]/'build'/'shader-profile-tests'
        test_root.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(dir=test_root)
        self.root = Path(self.tmp.name)
        self.code = bytes(range(128))
        self.env = dict(local_memory_size=0, texture_bound=2, guest_start_address=0,
                        guest_read_lowest=0, guest_read_highest=120,
                        viewport_transform_state=1, stage=5, texture_types=[],
                        texture_pixel_formats=[], cbuf_values=[[0, 17]],
                        cbuf_replacements=[], workgroup_size=[8, 4, 1], shared_memory_size=64)
        self.cache = MAGIC + struct.pack('<II', 20, 1) + env_bytes(self.env, self.code) + bytes(64)
        (self.root/'source.bin').write_bytes(self.cache)
        self.profile, _, _ = export_profile(self.root/'source.bin', '0100152000022000', 'synthetic')

    def tearDown(self):
        self.tmp.cleanup()

    def reference(self):
        (self.root/'game.bin').write_bytes(b'prefix'+self.code+b'suffix')
        self.profile['shaders'][sha(self.code)]['references'] = [
            dict(file='game.bin', encoding='raw', offset=6, size=128)]

    def test_manifest_only_roundtrip(self):
        self.reference()
        write_profile(self.root/'profile.nxshader', self.profile)
        (self.root/'source.bin').unlink()
        result = reconstruct(load_profile(self.root/'profile.nxshader'), self.root, self.root/'result.bin')
        self.assertEqual(result['rebuilt_pipelines'], 1)
        self.assertEqual((self.root/'result.bin').read_bytes(), self.cache)

    def test_missing_is_fail_closed(self):
        result = reconstruct(self.profile, self.root, self.root/'result.bin')
        self.assertEqual(result['rebuilt_pipelines'], 0)
        self.assertFalse((self.root/'result.bin').exists())

    def test_wrong_game_bytes(self):
        self.reference()
        (self.root/'game.bin').write_bytes(bytes(256))
        result = reconstruct(self.profile, self.root, self.root/'result.bin')
        self.assertEqual(result['resolved_shaders'], 0)

    def test_traversal_rejected(self):
        self.reference()
        self.profile['shaders'][sha(self.code)]['references'][0]['file'] = '../outside.bin'
        with self.assertRaises(ValueError):
            reconstruct(self.profile, self.root, self.root/'result.bin')

    def test_existing_cache_preserved(self):
        self.reference()
        (self.root/'result.bin').write_bytes(b'keep')
        with self.assertRaises(FileExistsError):
            reconstruct(self.profile, self.root, self.root/'result.bin')
        self.assertEqual((self.root/'result.bin').read_bytes(), b'keep')

    def test_cache_version_and_truncation(self):
        for invalid in (self.cache[:30], self.cache[:-1], self.cache[:8]+bytes(4)+self.cache[12:]):
            with self.assertRaises(ValueError):
                parse_cache(invalid)

    def test_no_shader_payload_and_determinism(self):
        raw = json.dumps(self.profile).encode()
        self.assertNotIn(self.code, raw)
        self.assertNotIn(self.code.hex().encode(), raw)
        write_profile(self.root/'one.nxshader', self.profile)
        write_profile(self.root/'two.nxshader', self.profile)
        self.assertEqual((self.root/'one.nxshader').read_bytes(), (self.root/'two.nxshader').read_bytes())

    def test_environment_tampering(self):
        self.profile['environments'][next(iter(self.profile['environments']))]['local_memory_size'] = 999
        write_profile(self.root/'profile.nxshader', self.profile)
        with self.assertRaises(ValueError):
            load_profile(self.root/'profile.nxshader')

    def test_yaz0_overlap_and_invalid_distance(self):
        raw = b'Yaz0'+struct.pack('>I', 8)+bytes(8)+b'\x80A\x50\x00'
        self.assertEqual(decode_yaz0(raw), b'A'*8)
        with self.assertRaises(ValueError):
            decode_yaz0(raw[:16]+b'\x00\x50\x00')

    def test_graphics_header_recovered_from_game(self):
        env = dict(self.env, stage=4, program_header_sha256=sha(self.code[:80]))
        key = bytearray(1400)
        struct.pack_into('<Q', key, 40, 123)
        raw = MAGIC+struct.pack('<II', 20, 1)+env_bytes(env, self.code)+key
        parsed = parse_cache(raw)
        self.assertEqual(parsed[0][0][0][0]['program_header_sha256'], sha(self.code[:80]))


if __name__ == '__main__':
    unittest.main()
