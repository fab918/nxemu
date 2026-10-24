"""Private cache-span diagnosis; report offsets/lengths, never shader instructions."""
import collections
import json
import struct
from nxshader import export_profile,sha
from rebuild_recipes import REPO,CACHE

work=REPO/'build/shader-origin-poc';work.mkdir(exist_ok=True)
profile=json.loads((REPO/'build/shader-bktr-poc/full-profile-logical.json').read_bytes())
_,codes,_=export_profile(CACHE,profile['title_id'],profile['content_label'])
terminals={0xE2400FFFFF87000F,0xE2400FFFFF07000F}
report=[]
for h,code in codes.items():
    if profile['shaders'][h]['references']:continue
    positions=[i for i in range(0,len(code),8) if struct.unpack_from('<Q',code,i)[0] in terminals]
    report.append(dict(shader=h,size=len(code),terminal_distances_from_end=[len(code)-i for i in positions],
                       last_word_zero=code[-8:]==bytes(8),trim8_sha256=sha(code[:-8]),
                       stages=sorted({e['stage'] for e in profile['environments'].values() if e['shader']==h})))
(work/'tail-audit.json').write_text(json.dumps(report,indent=2))
print(json.dumps(dict(shaders=len(report),terminal_distances=dict(collections.Counter(
    str(x['terminal_distances_from_end']) for x in report)),last_word_zero=sum(x['last_word_zero'] for x in report)),indent=2))
