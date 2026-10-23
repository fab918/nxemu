"""Bounded LZ4 block reader for the NSO segments already supported by NXEmu."""
def decode_lz4(raw,expected):
    if not 0<=expected<=512*1024*1024: raise ValueError('LZ4 output limit')
    p=0; out=bytearray()
    def length(value):
        nonlocal p
        if value==15:
            while True:
                if p>=len(raw): raise ValueError('truncated LZ4 length')
                x=raw[p]; p+=1; value+=x
                if x!=255: break
        return value
    while p<len(raw):
        token=raw[p]; p+=1
        literals=length(token>>4)
        if literals>len(raw)-p or literals>expected-len(out): raise ValueError('LZ4 literal bounds')
        out.extend(raw[p:p+literals]); p+=literals
        if p==len(raw): break
        if p+2>len(raw): raise ValueError('truncated LZ4 distance')
        distance=raw[p]|raw[p+1]<<8; p+=2
        amount=length(token&15)+4
        if distance==0 or distance>len(out) or amount>expected-len(out): raise ValueError('LZ4 backreference bounds')
        pattern=bytes(out[-distance:])
        out.extend((pattern*((amount+distance-1)//distance))[:amount])
    if len(out)!=expected: raise ValueError('LZ4 output size mismatch')
    return bytes(out)
