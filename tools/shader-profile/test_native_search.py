import mmap
import os
from pathlib import Path
import random
import re
import tempfile
import unittest
from native_search import NativeSearch


class NativeSearchTests(unittest.TestCase):
    def library(self):
        path=os.environ.get('NXSHADER_SEARCH_LIBRARY')
        if not path:self.skipTest('set NXSHADER_SEARCH_LIBRARY to test native discovery')
        return path

    def test_matches_reference_with_chunks_and_overlapping_prefixes(self):
        rng=random.Random(4)
        prefixes=[b'A'*16,b'A'*15+b'B',bytes(range(16)),bytes(16)]
        data=bytearray(rng.randbytes(12000))
        for pos in range(17,len(data)-50,53):data[pos:pos+16]=prefixes[pos%len(prefixes)]
        data[180:220]=b'A'*40
        data[-16:]=prefixes[2]
        data=bytes(data)
        expected=[m.start() for m in re.finditer(b'|'.join(re.escape(p) for p in sorted(prefixes)),data)]
        native=NativeSearch(self.library(),prefixes)
        try:
            self.assertEqual(list(native.positions(data)),expected)
            with tempfile.TemporaryDirectory() as directory:
                path=Path(directory)/'data';path.write_bytes(data)
                with path.open('rb') as file,mmap.mmap(file.fileno(),0,access=mmap.ACCESS_READ) as mapped:
                    for chunk in (16,17,31,64,4096):
                        self.assertEqual(list(native.positions(mapped,chunk)),expected)
            for size in range(16):self.assertEqual(list(native.positions(data[:size])),[])
        finally:native.close()

    def test_batches_and_close(self):
        native=NativeSearch(self.library(),[b'A'*16])
        try:self.assertEqual(list(native.positions(b'A'*(16*10000))),list(range(0,160000,16)))
        finally:native.close()
        native.close()

    def test_short_prefix_uses_reference_fallback(self):
        self.assertIsNone(NativeSearch(self.library(),[b'12345678']).handle)
