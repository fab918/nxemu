import sys,bisect,collections
mapf,rawf=sys.argv[1],sys.argv[2]
ents=[]
for l in open(mapf):
    p=l.split()
    if len(p)<3: continue
    ents.append((int(p[0],16),int(p[1],16),p[2]))
ents.sort()
starts=[e[0] for e in ents]
per=collections.defaultdict(collections.Counter)
tot=collections.Counter()
for l in open(rawf):
    tid,a=l.strip().split(','); a=int(a,16)
    i=bisect.bisect_right(starts,a)-1
    name=None
    if i>=0 and a<ents[i][0]+ents[i][1]: name=ents[i][2]
    tot[tid]+=1
    per[tid][name or 'other']+=1
for tid,c in per.items():
    jit=sum(v for k,v in c.items() if k!='other')
    if jit < tot[tid]*0.2: continue
    print(f"== thread {tid}: {tot[tid]} samples, in mapped JIT {100*jit/tot[tid]:.1f}%")
allc=collections.Counter()
for tid,c in per.items():
    jit=sum(v for k,v in c.items() if k!='other')
    if jit < tot[tid]*0.2: continue
    for k,v in c.items():
        if k!='other': allc[k]+=v
T=sum(allc.values())
print("total jit samples",T, "distinct blocks", len(allc))
special=collections.Counter()
for k,v in allc.items():
    if not k.startswith('a64_'): special[k]+=v
print("non-block code:",[(k,round(100*v/T,2)) for k,v in special.most_common()])
acc=0
for i,(k,v) in enumerate(allc.most_common(60)):
    acc+=v
    print(f"{100*v/T:5.2f}% cum {100*acc/T:5.1f}%  {k}")
# cumulative coverage
vals=sorted(allc.values(),reverse=True)
for n in (10,50,100,500,1000):
    print(n,"blocks:",round(100*sum(vals[:n])/T,1),"%")
