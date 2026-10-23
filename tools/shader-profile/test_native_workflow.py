"""Black-box regression tests. Production export/import runs entirely in C++."""
import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from nxshader import canonical
import test_portable_profile as reference


class NativeWorkflowTests(unittest.TestCase):
    def setUp(self):
        self.exporter=os.environ.get('NXSHADER_EXPORTER')
        self.reader=os.environ.get('NXSHADER_READER')
        if not self.exporter or not self.reader:self.skipTest('native exporter and reader required')
        self.temporary=tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root=Path(self.temporary.name)
        self.profile=reference.PortableProfileTests().fixture()
        (self.root/'game.dat').write_bytes(b'12345678')
        (self.root/'recording.json').write_bytes(canonical(dict(failed=False,projection_mismatches=0,
            dropped_observations=0,guest_state_word_count=278,feature_bits=67,recipes=[])))
        self.cache(self.profile,'source')

    def cache(self,profile,name):
        source=self.root/(name+'.json');source.write_bytes(canonical(profile))
        result=subprocess.run([self.reader,str(source),str(self.root),profile['title_id'],str(self.root/(name+'.nxc')),'67'],capture_output=True)
        self.assertEqual(result.returncode,0,result.stderr)

    def export(self,name,source='source',extra=()):
        return subprocess.run([self.exporter,str(self.root/(source+'.nxc')),str(self.root/'recording.json'),
            str(self.root),'game.dat','-',self.profile['title_id'],str(self.root/(name+'.nxshader')),
            str(self.root/'index.json'),'test',*extra],capture_output=True)

    def read(self,path,name='result'):
        return subprocess.run([self.reader,str(path),str(self.root),self.profile['title_id'],str(self.root/(name+'.nxc')),'67'],capture_output=True)

    def test_native_v2_and_v3_reconstruct_identical_caches(self):
        plain=self.export('v2');compact=self.export('v3',extra=('--compact',))
        self.assertEqual(plain.returncode,0,plain.stderr)
        self.assertEqual(compact.returncode,0,compact.stderr)
        self.assertEqual(json.loads(compact.stdout)['reused'],1)
        for name in ('v2','v3'):
            result=self.read(self.root/(name+'.nxshader'),name+'-read')
            self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual((self.root/'v2-read.nxc').read_bytes(),(self.root/'v3-read.nxc').read_bytes())
        self.assertEqual((self.root/'source.nxc').read_bytes(),(self.root/'v3-read.nxc').read_bytes())

    def test_index_reuses_verified_references_when_capture_grows(self):
        self.assertEqual(self.export('first').returncode,0)
        second=copy.deepcopy(self.profile)
        code=b'ABCDEFGH';old=next(iter(second['shaders']));new=hashlib.sha256(code).hexdigest()
        shader=second['shaders'].pop(old);shader['references'][0]['offset']=8;second['shaders'][new]=shader
        old_env=next(iter(second['environments']));env=second['environments'].pop(old_env);env['shader']=new
        eid=hashlib.sha256(canonical(env)).hexdigest();second['environments'][eid]=env
        second['recipes'][0]['environments']=[eid];second['recipes'][0]['shader_hash']='18446744073709551614'
        combined=copy.deepcopy(self.profile)
        for key in ('shaders','environments'):combined[key].update(second[key])
        combined['recipes']+=second['recipes']
        (self.root/'game.dat').write_bytes(b'12345678'+code)
        self.cache(combined,'grown')
        result=self.export('grown-result',source='grown')
        self.assertEqual(result.returncode,0,result.stderr)
        report=json.loads(result.stdout)
        self.assertEqual(report['reused'],1);self.assertEqual(report['programs'],2)

    def test_changed_local_program_is_rejected(self):
        self.assertEqual(self.export('first',extra=('--compact',)).returncode,0)
        (self.root/'game.dat').write_bytes(b'12345679')
        self.assertNotEqual(self.read(self.root/'first.nxshader').returncode,0)
        self.assertNotEqual(self.export('changed').returncode,0)
        self.assertFalse((self.root/'changed.nxshader').exists())

    def test_cancel_checkpoints_without_publishing_or_shader_code(self):
        result=self.export('cancelled',extra=('--cancel',))
        self.assertNotEqual(result.returncode,0)
        self.assertFalse((self.root/'cancelled.nxshader').exists())
        self.assertTrue((self.root/'index.json').exists())
        self.assertNotIn(b'12345678',(self.root/'index.json').read_bytes())
        self.assertEqual(self.export('resumed').returncode,0)

    def test_existing_output_is_preserved(self):
        target=self.root/'kept.nxshader';target.write_bytes(b'KEEP')
        self.assertNotEqual(self.export('kept').returncode,0)
        self.assertEqual(target.read_bytes(),b'KEEP')

    def test_native_journal_input_and_corrupt_frame_rejection(self):
        # A compute-only cache needs no graphics observations. It can still be
        # exported through the automatic journal workflow without a JSON report.
        journal=self.root/'recording.nxr';journal.write_bytes(b'NXREC001')
        args=[self.exporter,str(self.root/'source.nxc'),str(journal),str(self.root),
            'game.dat','-',self.profile['title_id'],str(self.root/'journal.nxshader'),
            str(self.root/'index.json'),'test','--compact']
        result=subprocess.run(args,capture_output=True)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(self.read(self.root/'journal.nxshader').returncode,0)
        journal.write_bytes(b'NXREC001'+bytes(1532)+b'0'*64)
        args[7]=str(self.root/'corrupt-journal.nxshader')
        result=subprocess.run(args,capture_output=True)
        self.assertNotEqual(result.returncode,0)
        self.assertFalse((self.root/'corrupt-journal.nxshader').exists())

    def test_compact_corruption_and_resource_traversal_are_rejected(self):
        self.assertEqual(self.export('v3',extra=('--compact',)).returncode,0)
        data=(self.root/'v3.nxshader').read_bytes()
        for i,length in enumerate((0,8,31,len(data)-1)):
            path=self.root/('truncated'+str(i));path.write_bytes(data[:length])
            self.assertNotEqual(self.read(path,'bad'+str(i)).returncode,0)
        source=self.root/'traversal.json'
        profile=copy.deepcopy(self.profile)
        next(iter(profile['shaders'].values()))['references'][0]['file']='../outside'
        source.write_bytes(canonical(profile));target=self.root/'traversal.nxshader'
        result=subprocess.run([self.exporter,'--compact',str(source),str(target)],capture_output=True)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertNotEqual(self.read(target).returncode,0)

    def test_external_query_enums_are_rejected_in_v2_and_v3(self):
        for index,field in enumerate(('texture_types','texture_pixel_formats','cbuf_replacements')):
            profile=copy.deepcopy(self.profile)
            next(iter(profile['environments'].values()))[field]=[[0,0xffffffff]]
            source=self.root/('bad-enum-'+str(index)+'.json');source.write_bytes(canonical(profile))
            self.assertNotEqual(self.read(source,'bad-v2-'+str(index)).returncode,0)
            target=self.root/('bad-enum-'+str(index)+'.nxshader')
            packed=subprocess.run([self.exporter,'--compact',str(source),str(target)],capture_output=True)
            self.assertEqual(packed.returncode,0,packed.stderr)
            self.assertNotEqual(self.read(target,'bad-v3-'+str(index)).returncode,0)
            self.assertFalse((self.root/('bad-v3-'+str(index)+'.nxc')).exists())
