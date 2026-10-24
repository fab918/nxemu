import copy
import struct
import unittest
from nxshader import canonical,sha
from shader_span import without_extra_cache_word,rebind_resource_span


class ShaderSpanTests(unittest.TestCase):
    def program(self,tail=b'padding!'):
        return bytes(80)+bytes(range(16))+struct.pack('<Q',0xE2400FFFFF87000F)+tail
    def test_only_extra_word_is_removed_and_terminal_preserved(self):
        code=self.program()
        result=without_extra_cache_word(code)
        self.assertEqual(result,code[:-8]);self.assertEqual(result[-8:],code[-16:-8])
    def test_unproven_spans_are_rejected(self):
        for data in [bytes(112),self.program()[:-8],self.program()+b'x',bytes(88)]:
            with self.subTest(length=len(data)),self.assertRaises(ValueError):without_extra_cache_word(data)
    def test_environment_bounds_change_but_combinations_and_keys_remain(self):
        code=self.program();old=sha(code)
        envs={}
        for value in (11,22):
            e=dict(shader=old,guest_read_lowest=0,guest_read_highest=len(code)-8,
                   cbuf_values=[[0,value]],program_header_sha256=sha(code[:80]))
            envs[sha(canonical(e))]=e
        pipelines=[dict(kind='graphics',key_le_hex=(bytes([i])+bytes(1399)).hex(),environments=[eid])
                   for i,eid in enumerate(envs)]
        p=dict(shaders={old:dict(size=len(code),references=[])},environments=envs,pipelines=pipelines,
               guest_recipe_probe=dict(observed_graphics_recipes=[1,2]))
        before=copy.deepcopy(p)
        new=rebind_resource_span(p,old,code,dict(file='game',encoding='raw',offset=7,size=len(code)-8))
        self.assertEqual(len(p['pipelines']),2)
        self.assertEqual([x['key_le_hex'] for x in p['pipelines']],[x['key_le_hex'] for x in before['pipelines']])
        self.assertEqual(len(p['environments']),2)
        self.assertEqual(sorted(e['cbuf_values'][0][1] for e in p['environments'].values()),[11,22])
        self.assertTrue(all(e['guest_read_highest']==len(code)-16 for e in p['environments'].values()))
        self.assertEqual(p['guest_recipe_probe'],before['guest_recipe_probe'])
        self.assertEqual(new,sha(code[:-8]));self.assertNotIn(old,p['shaders'])
    def test_wrong_reference_size_does_not_mutate_profile(self):
        code=self.program();p=dict(shaders={},environments={},pipelines=[]);before=copy.deepcopy(p)
        with self.assertRaises(ValueError):rebind_resource_span(p,sha(code),code,dict(size=1))
        self.assertEqual(p,before)


if __name__=='__main__':unittest.main()
