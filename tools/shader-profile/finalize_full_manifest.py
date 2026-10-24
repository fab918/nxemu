"""Attach verified logical locators to the already validated full recipe manifest."""
import copy
import json
from nxshader import canonical,write_new,write_profile,sha
from full_recipe_manifest import WORK

profile=json.loads((WORK/'full-profile.json').read_bytes())
locators=json.loads((WORK/'logical-verified-profile-controls.json').read_bytes())
assert set(locators['shaders'])==set(profile['shaders'])
profile['shaders']=locators['shaders']
write_new(WORK/'full-profile-logical.json',canonical(profile))
sizes=write_profile(WORK/'MarioKart-full-555-logical.nxshader',profile)
summary=dict(entries=len(profile['pipelines']),graphics=545,compute=10,preserved_graphics_recipes=1108,
             shaders=len(profile['shaders']),shaders_with_references=sum(bool(s['references']) for s in profile['shaders'].values()),
             json_bytes=sizes[0],manifest_bytes=sizes[1],
             manifest_sha256=sha((WORK/'MarioKart-full-555-logical.nxshader').read_bytes()),
             logical_bktr_shader_references=sum(r['encoding'].startswith('bktr-') for s in profile['shaders'].values() for r in s['references']))
(WORK/'final-manifest-summary.json').write_text(json.dumps(summary,indent=2))
print(json.dumps(summary,indent=2))
