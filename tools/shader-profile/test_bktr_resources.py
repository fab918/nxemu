import struct
import unittest
from bktr_resources import Physical,Slice,Indirect,bucket_entries,romfs_files,NODE,CAPACITY


class BktrTests(unittest.TestCase):
    def test_read_and_provenance_across_both_files(self):
        base=Physical(b'abcdefghijklmnop','base'); patch=Physical(b'XYZ!','update')
        view=Indirect(base,patch,[(0,2,0),(5,0,1),(8,12,0)],12)
        self.assertEqual(view.read(3,7),b'fgXYZmn')
        self.assertEqual(view.spans(3,7),[dict(file='base',offset=5,size=2),
                                        dict(file='update',offset=0,size=3),dict(file='base',offset=12,size=2)])
        self.assertEqual(Slice(view,3,7).read(1,5),b'gXYZm')
        for offset,size in [(-1,1),(12,1),(10,3)]:
            with self.assertRaises(ValueError): view.read(offset,size)

    def test_invalid_mapping_is_rejected(self):
        a=Physical(bytes(16),'base'); b=Physical(bytes(16),'patch')
        for entries,end in [([(1,0,0)],4),([(0,0,2)],4),([(0,15,0)],4),
                            ([(0,0,0),(0,0,1)],4)]:
            with self.assertRaises(ValueError): Indirect(a,b,entries,end)

    def table(self,count):
        sets=(count+CAPACITY-1)//CAPACITY
        data=bytearray(512+(sets+1)*NODE)
        h=bytearray(512)
        struct.pack_into('<QQ4sIii',h,0x100,512,(sets+1)*NODE,b'BKTR',1,count,0)
        struct.pack_into('<iiq',data,512,0,sets,count)
        for leaf in range(sets):
            first=leaf*CAPACITY; n=min(CAPACITY,count-first)
            pos=512+NODE+leaf*NODE
            struct.pack_into('<q',data,512+16+leaf*8,first)
            struct.pack_into('<iiq',data,pos,leaf,n,first+n)
            for j in range(n): struct.pack_into('<qqi',data,pos+16+j*20,first+j,first+j,0)
        return data,h

    def test_bucket_leaf_boundary_and_declared_count(self):
        data,h=self.table(CAPACITY+1)
        entries,end,offset=bucket_entries(Physical(data,'patch'),h)
        self.assertEqual(len(entries),CAPACITY+1); self.assertEqual(end,CAPACITY+1)
        struct.pack_into('<q',data,512+NODE+8,CAPACITY-1)
        with self.assertRaises(ValueError): bucket_entries(Physical(data,'patch'),h)

    def test_truncated_bucket_and_invalid_version(self):
        data,h=self.table(2)
        with self.assertRaises(ValueError): bucket_entries(Physical(data[:-1],'patch'),h)
        struct.pack_into('<I',h,0x114,99)
        with self.assertRaises(ValueError): bucket_entries(Physical(data,'patch'),h)

    def test_romfs_file_payload_spans_patch_boundary(self):
        data=bytearray(154)
        struct.pack_into('<10Q',data,0,80,80,0,80,24,104,0,104,40,144)
        struct.pack_into('<6I',data,80,0xffffffff,0xffffffff,0xffffffff,0,0xffffffff,0)
        struct.pack_into('<IIQQII',data,104,0,0xffffffff,0,10,0xffffffff,6)
        data[136:142]=b'shader'; data[144:]=b'abcdefghij'
        base=Physical(bytes(data),'base'); patch=Physical(b'XYZ','patch')
        logical=Indirect(base,patch,[(0,0,0),(148,0,1),(151,151,0)],154)
        files=list(romfs_files(logical))
        self.assertEqual(files,[dict(name='/shader',offset=144,size=10)])
        self.assertEqual(logical.read(files[0]['offset'],files[0]['size']),b'abcdXYZhij')
        corrupt=bytearray(data); struct.pack_into('<I',corrupt,108,0)
        with self.assertRaises(ValueError): list(romfs_files(Physical(corrupt,'bad')))


if __name__=='__main__': unittest.main()
