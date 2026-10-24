import struct
import unittest
from compare_full_replay import normalize_compute_key


class ComputeComparisonTests(unittest.TestCase):
    def key(self,unused=512,used=1024,group=8,shared=128):
        return struct.pack('<QI3I8I2I',123,shared,group,1,1,used,unused,0,0,0,0,0,0,0,0)
    def test_unused_sizes_do_not_create_false_mismatches(self):
        self.assertEqual(normalize_compute_key(self.key(512),1),normalize_compute_key(self.key(8192),1))
    def test_used_sizes_and_workgroup_shared_memory_remain_distinct(self):
        reference=normalize_compute_key(self.key(),1)
        for key in [self.key(used=2048),self.key(group=16),self.key(shared=256)]:
            self.assertNotEqual(reference,normalize_compute_key(key,1))
        self.assertNotEqual(normalize_compute_key(self.key(512),2),normalize_compute_key(self.key(8192),2))
    def test_invalid_key_size_is_rejected(self):
        with self.assertRaises(ValueError): normalize_compute_key(bytes(63),0)


if __name__=='__main__':unittest.main()
