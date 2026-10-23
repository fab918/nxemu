"""Bounded reader for NXSCACHE storage revision 1, shader ABI 20."""
import hashlib
import struct


def legacy_bytes(data):
    if len(data)>256*1024*1024 or data[:16]!=b'NXSCACHE'+struct.pack('<II',1,20):
        raise ValueError('unsupported local shader cache')
    cursor=16; codes={}; environments={}; seen=set(); output=bytearray(b'yuzucach'+struct.pack('<I',20))
    while cursor<len(data):
        if len(data)-cursor<37: break  # Interrupted final append.
        kind,size=struct.unpack_from('<BI',data,cursor)
        if not 1<=kind<=3 or size>4*1024*1024: raise ValueError('invalid local cache frame')
        if size>len(data)-cursor-37: break
        digest=data[cursor+5:cursor+37]; body=data[cursor+37:cursor+37+size];cursor+=37+size
        if hashlib.sha256(bytes([kind])+body).digest()!=digest: raise ValueError('local cache frame fingerprint')
        if kind==1:
            if not body or len(body)%8 or len(body)>1024*1024: raise ValueError('local shader bounds')
            codes[digest]=body
        elif kind==2:
            if len(body)<100 or body[68:100] not in codes: raise ValueError('local environment reference')
            code=codes[body[68:100]]
            if struct.unpack_from('<Q',body)[0]!=len(code): raise ValueError('local code size')
            environments[digest]=body[:68]+code+body[100:]
        else:
            if len(body)<4: raise ValueError('local pipeline header')
            count,=struct.unpack_from('<I',body)
            if not 1<=count<=6 or len(body)<8+32*count: raise ValueError('local pipeline references')
            size,=struct.unpack_from('<I',body,4+32*count)
            if size not in (64,1400) or len(body)!=8+32*count+size: raise ValueError('local pipeline key')
            if digest in seen:continue
            seen.add(digest);output+=struct.pack('<I',count)
            for i in range(count): output+=environments[body[4+32*i:36+32*i]]
            output+=body[8+32*count:]
        if len(output)>256*1024*1024:raise ValueError('expanded cache limit')
    return bytes(output)
