import struct
import unittest
from nso_resources import decode_lz4


class NsoTests(unittest.TestCase):
    def test_literal_block(self):
        self.assertEqual(decode_lz4(b'\x30abc',3),b'abc')
    def test_overlapping_backreference(self):
        self.assertEqual(decode_lz4(b'\x32abc\x03\x00',9),b'abcabcabc')
    def test_extended_literal_length(self):
        raw=b'\xf0\x05'+b'x'*20
        self.assertEqual(decode_lz4(raw,20),b'x'*20)
    def test_invalid_distance_truncation_and_output_size(self):
        for raw,size in [(b'\x10x\x00\x00',5),(b'\xf0',10),(b'\x30ab',3),(b'\x30abc',4),
                         (b'\x30abc\x04\x00',7),(b'\x30abc\x03\x00',6)]:
            with self.subTest(raw=raw),self.assertRaises(ValueError): decode_lz4(raw,size)
    def test_output_limit(self):
        with self.assertRaises(ValueError): decode_lz4(b'',512*1024*1024+1)


if __name__=='__main__':unittest.main()
