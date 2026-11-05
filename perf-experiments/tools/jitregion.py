import sys,bisect,collections
ents=[]
for l in open(sys.argv[1]):
    p=l.split()
    if len(p)>=3: ents.append((int(p[0],16),int(p[1],16),p[2]))
ents.sort(); starts=[e[0] for e in ents]
c=collections.Counter(); T=0
for l in open(sys.argv[2]):
    tid,a=l.strip().split(','); a=int(a,16)
    i=bisect.bisect_right(starts,a)-1
    if i>=0 and a<ents[i][0]+ents[i][1] and ents[i][2].startswith("a64_0"):
        pc=int(ents[i][2][4:20],16); c[pc]+=1; T+=1
for shift,label in ((12,'4K'),(16,'64K'),(20,'1M')):
    r=collections.Counter()
    for pc,v in c.items(): r[pc>>shift]+=v
    print('--',label, [(hex(k<<shift),round(100*v/T,1)) for k,v in r.most_common(12)])
