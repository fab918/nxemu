"""Targeted tests for physical-stream bounds and the permitted existing HLE alias."""
import ctypes as c
import os
from pathlib import Path
import struct
import unittest
from compare_rebuild_trace import compare
from rebuild_recipes import REPO


class RebuildTests(unittest.TestCase):
    def test_physical_yaz0_exact_consumed_extent(self):
        library=os.environ.get('NXSHADER_SEARCH_LIBRARY')
        if not library:
            library=REPO/'build/shader-recipe-poc/tests/Release/yaz0-scan.dll'
            if not library.exists():self.skipTest('set NXSHADER_SEARCH_LIBRARY to test native Yaz0 bounds')
        dll=c.CDLL(str(library))
        decode=dll.nxshader_yaz0_extent
        decode.argtypes=[c.c_void_p,c.c_size_t,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)]
        raw=b'Yaz0'+struct.pack('>I',3)+bytes(8)+b'\xe0abc'+b'trailing bytes'
        out=c.create_string_buffer(3); consumed=c.c_size_t()
        self.assertEqual(decode(raw,len(raw),out,3,c.byref(consumed)),0)
        self.assertEqual(out.raw,b'abc'); self.assertEqual(consumed.value,20)
        self.assertNotEqual(decode(raw,19,out,3,c.byref(consumed)),0)
        invalid=b'Yaz0'+struct.pack('>I',3)+bytes(8)+bytes(3)
        self.assertNotEqual(decode(invalid,len(invalid),out,3,c.byref(consumed)),0)

    def setup_alias(self):
        plain=bytearray(480)
        struct.pack_into('<6Q',plain,0,0,1,0,0,0,2)
        struct.pack_into('<I',plain,416,63)
        hle=bytearray(plain); struct.pack_into('<I',hle,420,1<<17)
        def products():
            return {'native-success':[dict(path='native.bin')],
                    'ir':[dict(stage=i,sha256='ir') for i in (1,5)],
                    'spirv':[dict(stage=i,sha256='spirv') for i in (1,5)]}
        case=dict(recipe_id=7,pipeline=dict(key_le_hex=(bytes(hle)+bytes(920)).hex(),environments=['env']),
                  recipe=dict(shader_hashes=[0,1,0,0,0,2]))
        return bytes(plain),bytes(hle),products,case

    def test_existing_hle_twin_is_verified_without_merging_recipe(self):
        plain,hle,products,case=self.setup_alias()
        result=compare({plain:products()},{hle:products()},[case],{'env':dict(cbuf_replacements=[])})
        self.assertEqual(result['complete_successes'],1)
        self.assertEqual(result['matching_effective_native_keys'],0)
        self.assertEqual(result['live_hle_aliases'],1)

    def test_hle_replacements_prevent_alias(self):
        plain,hle,products,case=self.setup_alias()
        result=compare({plain:products()},{hle:products()},[case],{'env':dict(cbuf_replacements=[[0,1]])})
        self.assertEqual(result['complete_successes'],0)

    def test_spirv_difference_is_not_hidden_by_alias(self):
        plain,hle,products,case=self.setup_alias()
        changed=products(); changed['spirv'][0]['sha256']='different'
        result=compare({plain:products()},{hle:changed},[case],{'env':dict(cbuf_replacements=[])})
        self.assertEqual(result['complete_successes'],0)


if __name__=='__main__': unittest.main()
