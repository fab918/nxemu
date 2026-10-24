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
        second['recipes'][0]['environments']=[eid];second['recipes'][0]['shader_hash']=str(reference.native_program_hash(code))
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

    def test_index_reuses_raw_offset_beyond_decoded_resource_limit(self):
        self.assertEqual(self.export('first').returncode,0)
        offset=512*1024*1024+16
        # A raw file offset is independent of the bounded decoded resource size.
        # Use a sparse file so this also verifies reuse without a full raw scan.
        with (self.root/'game.dat').open('wb') as game:
            game.seek(offset)
            game.write(b'12345678')
        path=self.root/'index.json'
        index=json.loads(path.read_bytes())
        next(iter(index['references'].values()))['offset']=offset
        index.pop('checksum')
        index['checksum']=hashlib.sha256(canonical(index)).hexdigest()
        path.write_bytes(canonical(index))
        result=self.export('large-offset')
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(json.loads(result.stdout)['reused'],1)

    def test_cancel_checkpoints_without_publishing_or_shader_code(self):
        result=self.export('cancelled',extra=('--cancel',))
        self.assertNotEqual(result.returncode,0)
        self.assertFalse((self.root/'cancelled.nxshader').exists())
        self.assertTrue((self.root/'index.json').exists())
        self.assertNotIn(b'12345678',(self.root/'index.json').read_bytes())
        self.assertEqual(self.export('resumed').returncode,0)

    def test_import_cancellation_retry_and_repeated_imports(self):
        for fmt,extra in (('json',()),('compact',('--compact',))):
            profile=self.root/(fmt+'.nxshader')
            self.assertEqual(self.export(fmt,extra=extra).returncode,0)
            output=self.root/(fmt+'-cancel.nxc')
            args=[self.reader,str(profile),str(self.root),self.profile['title_id'],str(output),'67']
            cancelled=subprocess.run([*args,'--cancel'],capture_output=True)
            self.assertNotEqual(cancelled.returncode,0,cancelled.stdout)
            self.assertIn(b'cancelled',cancelled.stderr)
            self.assertFalse(output.exists())
            self.assertEqual(subprocess.run(args,capture_output=True).returncode,0)
            before=output.read_bytes()
            self.assertNotEqual(subprocess.run(args,capture_output=True).returncode,0)
            self.assertEqual(output.read_bytes(),before)
            retry=self.read(profile,fmt+'-retry')
            self.assertEqual(retry.returncode,0,retry.stderr)
            self.assertEqual((self.root/(fmt+'-retry.nxc')).read_bytes(),before)

    def test_inspection_validates_metadata_without_game_resource_io(self):
        for fmt,extra in (('inspect-json',()),('inspect-compact',('--compact',))):
            self.assertEqual(self.export(fmt,extra=extra).returncode,0)
        (self.root/'game.dat').unlink()
        for fmt in ('inspect-json','inspect-compact'):
            args=[self.reader,str(self.root/(fmt+'.nxshader')),str(self.root),self.profile['title_id'],str(self.root/'never.nxc'),'67','--inspect']
            inspected=subprocess.run(args,capture_output=True)
            self.assertEqual(inspected.returncode,0,inspected.stderr)
            self.assertEqual(json.loads(inspected.stdout)['compute'],1)
            self.assertFalse((self.root/'never.nxc').exists())
            args[3]='0000000000000001'
            self.assertNotEqual(subprocess.run(args,capture_output=True).returncode,0)
            self.assertNotEqual(self.read(self.root/(fmt+'.nxshader'),fmt+'-missing').returncode,0)

    def mixed_profile(self):
        mixed=copy.deepcopy(self.profile)
        code=b'ABCDEFGH';digest=hashlib.sha256(code).hexdigest()
        env=copy.deepcopy(next(iter(mixed['environments'].values())));env['shader']=digest
        eid=hashlib.sha256(canonical(env)).hexdigest()
        mixed['shaders'][digest]=dict(size=8,references=[dict(encoding='raw',file='update.dat',offset=0,size=8)])
        mixed['environments'][eid]=env
        recipe=copy.deepcopy(mixed['recipes'][0]);recipe['shader_hash']=str(reference.native_program_hash(code));recipe['environments']=[eid]
        mixed['recipes'].append(recipe)
        return mixed

    def pack(self,profile,name):
        source=self.root/(name+'.json');source.write_bytes(canonical(profile))
        target=self.root/(name+'.nxshader')
        result=subprocess.run([self.exporter,'--compact',str(source),str(target)],capture_output=True)
        self.assertEqual(result.returncode,0,result.stderr)
        return source,target

    def test_partial_import_retains_independent_recipes(self):
        mixed=self.mixed_profile()
        (self.root/'update.dat').write_bytes(b'ABCDEFGH')
        source,target=self.pack(mixed,'mixed')
        for index,path in enumerate((source,target)):
            self.assertEqual(json.loads(self.read(path,'all'+str(index)).stdout)['recipes'],2)
        (self.root/'update.dat').unlink()
        for index,path in enumerate((source,target)):
            result=self.read(path,'partial'+str(index))
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertEqual(json.loads(result.stdout)['recipes'],1)
        (self.root/'game.dat').unlink()
        self.assertNotEqual(self.read(target,'none').returncode,0)

    def test_incoherent_hash_and_dimensions_reject_entire_import(self):
        for field,value in [('shader_hash','1'),('workgroup_size',[2,3,4]),('shared_memory_size',32),('texture_table_size',99)]:
            bad=self.mixed_profile();bad['recipes'][0][field]=value
            (self.root/'update.dat').write_bytes(b'ABCDEFGH')
            source,target=self.pack(bad,'incoherent-'+field)
            for index,path in enumerate((source,target)):
                result=self.read(path,field+str(index))
                self.assertNotEqual(result.returncode,0,result.stdout)
                self.assertFalse((self.root/(field+str(index)+'.nxc')).exists())

    def test_inspection_rejects_invalid_bounds_without_resources(self):
        bad=copy.deepcopy(self.profile)
        next(iter(bad['environments'].values()))['guest_read_highest']=8
        source,target=self.pack(bad,'invalid-bounds')
        (self.root/'game.dat').unlink()
        for path in (source,target):
            result=subprocess.run([self.reader,str(path),str(self.root),bad['title_id'],str(self.root/'never.nxc'),'67','--inspect'],capture_output=True)
            self.assertNotEqual(result.returncode,0)

    def test_renamed_source_container_is_verified(self):
        result=self.export('portable',extra=('--compact',))
        self.assertEqual(result.returncode,0,result.stderr)
        (self.root/'game.dat').rename(self.root/'renamed-game.dat')
        read=self.read(self.root/'portable.nxshader','renamed')
        self.assertEqual(read.returncode,0,read.stderr)
        (self.root/'renamed-game.dat').write_bytes(b'87654321')
        self.assertNotEqual(self.read(self.root/'portable.nxshader','different').returncode,0)

    def test_merged_update_contexts_select_only_applicable_recipes(self):
        mixed=self.mixed_profile()
        a,b='a'*64,'b'*64
        mixed['source_context']=a
        mixed['recipes'][1]['source_context']=b
        source,target=self.pack(mixed,'contexts')
        for index,path in enumerate((source,target)):
            args=[self.reader,str(path),str(self.root),mixed['title_id'],str(self.root/('context-'+str(index)+'.nxc')),'67','--context='+a]
            result=subprocess.run(args,capture_output=True)
            self.assertEqual(result.returncode,0,result.stderr)
            report=json.loads(result.stdout)
            self.assertEqual(report['recipes'],1)
            self.assertEqual(report['incompatible_contexts'],1)
            self.assertEqual(report['missing_resources'],0)
        args[4]=str(self.root/'wrong-context.nxc');args[-1]='--context='+'c'*64
        self.assertNotEqual(subprocess.run(args,capture_output=True).returncode,0)
        self.assertFalse((self.root/'wrong-context.nxc').exists())

    def test_revision_reuses_verified_recipes_without_old_resources(self):
        _,r2=self.pack(self.profile,'revision-2')
        first=self.read(r2,'revision-2-cache')
        self.assertEqual(first.returncode,0,first.stderr)
        known=json.loads(first.stdout)['recipe_ids']
        mixed=self.mixed_profile();mixed['content_label']='revision 3'
        next(iter(mixed['shaders'].values()))['references'][0]['file']='old-resource-no-longer-needed.dat'
        _,r3=self.pack(mixed,'revision-3')
        (self.root/'game.dat').unlink()
        (self.root/'update.dat').write_bytes(b'ABCDEFGH')
        reuse=self.root/'verified-recipes.json';reuse.write_bytes(canonical(known))
        args=[self.reader,str(r3),str(self.root),mixed['title_id'],str(self.root/'revision-3-cache.nxc'),'67','--reuse='+str(reuse)]
        second=subprocess.run(args,capture_output=True)
        self.assertEqual(second.returncode,0,second.stderr)
        report=json.loads(second.stdout)
        self.assertEqual(report['recipes'],1);self.assertEqual(report['reused'],1)
        self.assertEqual(report['missing_resources'],0)
        reuse.write_bytes(canonical(report['recipe_ids']))
        (self.root/'update.dat').unlink();args[4]=str(self.root/'fully-reused.nxc')
        third=subprocess.run(args,capture_output=True)
        self.assertEqual(third.returncode,0,third.stderr)
        self.assertEqual(json.loads(third.stdout)['reused'],2)
        self.assertFalse((self.root/'fully-reused.nxc').exists())

    def test_new_recipe_reuses_stored_program_without_game_io(self):
        _,profile=self.pack(self.profile,'program-source')
        first=self.read(profile,'program-source-cache')
        self.assertEqual(first.returncode,0,first.stderr)
        changed=copy.deepcopy(self.profile)
        changed['recipes'][0]['workgroup_size']=[2,2,2]
        next(iter(changed['environments'].values()))['workgroup_size']=[2,2,2]
        _,profile=self.pack(changed,'new-combination')
        (self.root/'game.dat').unlink()
        args=[self.reader,str(profile),str(self.root),changed['title_id'],str(self.root/'new-combination-cache.nxc'),'67','--program-cache='+str(self.root/'program-source-cache.nxc')]
        result=subprocess.run(args,capture_output=True)
        self.assertEqual(result.returncode,0,result.stderr)
        report=json.loads(result.stdout)
        self.assertEqual(report['recipes'],1);self.assertEqual(report['reused'],0)
        self.assertEqual(report['missing_resources'],0)

    def test_runtime_accepts_only_the_new_compressed_wire_format(self):
        metadata=self.root/'runtime-metadata.json';metadata.write_bytes(canonical(self.profile))
        args=[self.reader,str(metadata),str(self.root),self.profile['title_id'],str(self.root/'never-runtime.nxc'),'67','--runtime']
        self.assertNotEqual(subprocess.run(args,capture_output=True).returncode,0)
        codec=Path(self.reader).with_name('nxshader-codec'+Path(self.reader).suffix)
        for index,payload in enumerate((b'NXSHBIN3'+bytes(400),canonical(self.profile))):
            raw=self.root/('obsolete-'+str(index));raw.write_bytes(payload)
            packed=self.root/('obsolete-'+str(index)+'.nxshader')
            subprocess.run([str(codec),'pack',str(raw),str(packed)],check=True,capture_output=True)
            args[1]=str(packed)
            self.assertNotEqual(subprocess.run(args,capture_output=True).returncode,0)
        self.assertFalse((self.root/'never-runtime.nxc').exists())

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

    def test_truncated_raw_container_preserves_independent_program(self):
        profile=self.mixed_profile()
        for shader in profile['shaders'].values():
            ref=shader['references'][0]
            if ref['file']=='update.dat':ref.update(file='game.dat',offset=8)
        _,packed=self.pack(profile,'truncated-container')
        result=self.read(packed,'truncated-independent')
        self.assertEqual(result.returncode,0,result.stderr)
        report=json.loads(result.stdout)
        self.assertEqual(report['recipes'],1)
        self.assertEqual(report['skipped'],1)
        self.assertEqual(report['incompatible_resources'],1)

    def test_named_container_is_preferred_over_same_header_decoy(self):
        profile=copy.deepcopy(self.profile)
        prefix=b'P'*4096
        (self.root/'game.dat').write_bytes(prefix+b'12345678')
        (self.root/'aaa-decoy.dat').write_bytes(prefix+b'87654321')
        next(iter(profile['shaders'].values()))['references'][0]['offset']=4096
        profile['source_files']=[dict(name='game.dat',size=4104,header=hashlib.sha256(prefix).hexdigest())]
        _,packed=self.pack(profile,'named-container')
        result=self.read(packed,'named-preferred')
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(json.loads(result.stdout)['recipes'],1)

    def test_unused_source_fingerprints_are_not_read(self):
        profile=self.mixed_profile()
        contexts=['a'*64,'b'*64]
        for recipe,context in zip(profile['recipes'],contexts):recipe['source_context']=context
        (self.root/'update.dat').write_bytes(b'ABCDEFGH')
        profile['source_files']=[dict(name=name,size=8,header=hashlib.sha256(code).hexdigest())
            for name,code in [('game.dat',b'12345678'),('update.dat',b'ABCDEFGH')]]
        _,packed=self.pack(profile,'unused-source')
        env=dict(os.environ,NXEMU_NXSHADER_TIMING='1')
        result=subprocess.run([self.reader,str(packed),str(self.root),profile['title_id'],
            str(self.root/'context-only.nxc'),'67','--context='+contexts[0]],capture_output=True,env=env)
        self.assertEqual(result.returncode,0,result.stderr)
        report=json.loads(result.stdout)
        self.assertEqual(report['recipes'],1)
        self.assertEqual(report['incompatible_contexts'],1)
        self.assertEqual(report['timings']['counts']['resource_read_bytes'],16)

    def test_foreign_version_is_staged_without_target_context_association(self):
        profile=copy.deepcopy(self.profile);source='a'*64;target='b'*64
        profile['source_context']=source
        _,packed=self.pack(profile,'foreign-version')
        output=self.root/'candidate.nxc'
        result=subprocess.run([self.reader,str(packed),str(self.root),profile['title_id'],
            str(output),'67','--context='+target,'--cross-version'],capture_output=True)
        self.assertEqual(result.returncode,0,result.stderr)
        report=json.loads(result.stdout)
        self.assertEqual(report['recipes'],1);self.assertEqual(report['candidates'],1)
        self.assertTrue(Path(str(output)+'.context-'+source).exists())
        self.assertFalse(Path(str(output)+'.context-'+target).exists())

    def test_cross_version_mode_keeps_exact_and_foreign_recipes_distinct(self):
        profile=self.mixed_profile();profile['source_context']='a'*64
        profile['recipes'][1]['source_context']='b'*64
        (self.root/'update.dat').write_bytes(b'ABCDEFGH')
        _,packed=self.pack(profile,'cross-merged')
        output=self.root/'cross-merged.nxc'
        result=subprocess.run([self.reader,str(packed),str(self.root),profile['title_id'],
            str(output),'67','--context='+'b'*64,'--cross-version'],capture_output=True)
        self.assertEqual(result.returncode,0,result.stderr)
        report=json.loads(result.stdout)
        self.assertEqual(report['recipes'],2);self.assertEqual(report['candidates'],1)
        for context in ('a'*64,'b'*64):
            # Header + exactly one checked record, never both target-associated.
            self.assertEqual(Path(str(output)+'.context-'+context).stat().st_size,72)

    def test_changed_foreign_resource_does_not_drop_unchanged_program(self):
        profile=self.mixed_profile();profile['source_context']='a'*64
        (self.root/'update.dat').write_bytes(b'ABCDEFGI')
        _,packed=self.pack(profile,'cross-partial')
        result=subprocess.run([self.reader,str(packed),str(self.root),profile['title_id'],
            str(self.root/'cross-partial.nxc'),'67','--context='+'b'*64,'--cross-version'],capture_output=True)
        self.assertEqual(result.returncode,0,result.stderr)
        report=json.loads(result.stdout)
        self.assertEqual(report['recipes'],1);self.assertEqual(report['candidates'],1)
        self.assertEqual(report['skipped'],1)

    def test_cross_version_never_guesses_unknown_provenance(self):
        _,packed=self.pack(self.profile,'unknown-source')
        result=subprocess.run([self.reader,str(packed),str(self.root),self.profile['title_id'],
            str(self.root/'unknown-source.nxc'),'67','--context='+'b'*64,'--cross-version'],capture_output=True)
        self.assertNotEqual(result.returncode,0)
        self.assertFalse((self.root/'unknown-source.nxc').exists())

    def test_reader_progress_completes_after_resource_verification(self):
        profile=self.mixed_profile()
        (self.root/'update.dat').write_bytes(b'ABCDEFGH')
        _,packed=self.pack(profile,'progress')
        report=json.loads(self.read(packed,'progress-result').stdout)
        self.assertGreater(report['progress_total'],0)
        self.assertEqual(report['progress_done'],report['progress_total'])
