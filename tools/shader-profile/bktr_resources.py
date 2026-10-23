"""Read-only logical BKTR + RomFS for plaintext DNCA fixtures.

Layout/range rules follow NXEmu's fssystem_{nca_file_system_driver,indirect_storage,
bucket_tree}. No keys, renderer hook, or assumption that virtual offsets are physical.
"""
from bisect import bisect_right
from contextlib import ExitStack
import mmap
from pathlib import Path
import struct
from game_resources import pfs_entries, decode_yaz0

NODE=0x4000
CAPACITY=(NODE-16)//20


def bounds(offset,size,limit):
    if offset<0 or size<0 or offset>limit or size>limit-offset:
        raise ValueError('storage read outside bounds')


class Physical:
    def __init__(self,data,name): self.data,self.name,self.size=data,name,len(data)
    def read(self,offset,size):
        bounds(offset,size,self.size); return self.data[offset:offset+size]
    def spans(self,offset,size):
        bounds(offset,size,self.size)
        return [dict(file=self.name,offset=offset,size=size)] if size else []


class Slice:
    def __init__(self,source,offset,size):
        bounds(offset,size,source.size)
        self.source,self.offset,self.size=source,offset,size
    def read(self,offset,size):
        bounds(offset,size,self.size); return self.source.read(self.offset+offset,size)
    def spans(self,offset,size):
        bounds(offset,size,self.size); return self.source.spans(self.offset+offset,size)


class Indirect:
    def __init__(self,base,patch,entries,end):
        if not entries or entries[0][0]!=0 or end<=0:
            raise ValueError('invalid BKTR logical coverage')
        self.sources=(base,patch); self.entries=entries; self.size=end
        self.starts=[x[0] for x in entries]
        for i,(virtual,physical,index) in enumerate(entries):
            stop=self.starts[i+1] if i+1<len(entries) else end
            if virtual<0 or stop<=virtual or index not in (0,1):
                raise ValueError('invalid BKTR entry order/index')
            bounds(physical,stop-virtual,self.sources[index].size)
    def chunks(self,offset,size):
        bounds(offset,size,self.size)
        stop=offset+size
        while offset<stop:
            i=bisect_right(self.starts,offset)-1
            virtual,physical,index=self.entries[i]
            end=min(stop,self.starts[i+1] if i+1<len(self.entries) else self.size)
            yield self.sources[index],physical+offset-virtual,end-offset
            offset=end
    def read(self,offset,size):
        return b''.join(source.read(physical,length) for source,physical,length in self.chunks(offset,size))
    def spans(self,offset,size):
        return [span for source,physical,length in self.chunks(offset,size)
                for span in source.spans(physical,length)]


def bucket_entries(section,header):
    offset,size=struct.unpack_from('<QQ',header,0x100)
    magic,version,count,reserved=struct.unpack_from('<4sIii',header,0x110)
    if magic!=b'BKTR' or version!=1 or not 0<count<=10_000_000 or reserved:
        raise ValueError('unsupported/invalid BKTR header')
    bounds(offset,size,section.size)
    sets=(count+CAPACITY-1)//CAPACITY
    per_node=(NODE-16)//8
    l2=0
    if sets>per_node:
        upper=(sets+per_node-1)//per_node
        l2=(sets-(per_node-(upper-1))+per_node-1)//per_node
        if upper>per_node: raise ValueError('BKTR tree too deep')
    nodes=(1+l2)*NODE
    if nodes+sets*NODE>size: raise ValueError('truncated BKTR table')
    meta=section.read(offset,nodes+sets*NODE)
    index,root_count,end=struct.unpack_from('<iiq',meta)
    if index!=0 or not 0<root_count<=per_node or end<=0:
        raise ValueError('invalid BKTR root node')
    entries=[]; previous_end=None
    for leaf in range(sets):
        pos=nodes+leaf*NODE
        index,n,leaf_end=struct.unpack_from('<iiq',meta,pos)
        if index!=leaf or not 0<n<=CAPACITY or len(entries)+n>count:
            raise ValueError('invalid BKTR entry-set header')
        block=[struct.unpack_from('<qqi',meta,pos+16+j*20) for j in range(n)]
        if previous_end is not None and block[0][0]!=previous_end:
            raise ValueError('BKTR leaf boundary mismatch')
        if leaf_end<=block[-1][0]: raise ValueError('invalid BKTR leaf end')
        previous_end=leaf_end; entries.extend(block)
    if len(entries)!=count or previous_end!=end: raise ValueError('BKTR count/end mismatch')
    return entries,end,offset


def sections(physical):
    # pfs_entries operates on the outer mmap, and checks member bounds.
    for name,nca,size in pfs_entries(physical.data):
        if not name.endswith('.nca'): continue
        h=physical.read(nca+0x200,0x200)
        if h[:4]!=b'DNCA': raise ValueError('logical reader accepts plaintext DNCA only')
        content_type=h[5]; program_id=struct.unpack_from('<Q',h,0x10)[0]
        content_index=struct.unpack_from('<I',h,0x18)[0]
        for index in range(4):
            first,last=struct.unpack_from('<II',h,0x40+index*16)
            if first==last: continue
            bounds(first*512,(last-first)*512,size)
            fs=physical.read(nca+0x400+index*512,512)
            yield dict(name=name,index=index,program_id=program_id,content_index=content_index,
                       content_type=content_type,header=fs,
                       storage=Slice(physical,nca+first*512,(last-first)*512))


def romfs_range(header):
    if header[2]!=0 or header[3]!=3: raise ValueError('only IVFC RomFS is supported')
    layers=struct.unpack_from('<I',header,20)[0]
    if not 2<=layers<=7: raise ValueError('invalid IVFC levels')
    return struct.unpack_from('<QQ',header,24+(layers-2)*24)


def romfs_files(storage):
    h=struct.unpack('<10Q',storage.read(0,80))
    if h[0]!=80: raise ValueError('invalid logical RomFS header')
    for i in (1,3,5,7): bounds(h[i],h[i+1],storage.size)
    if h[4]>128*1024*1024 or h[8]>128*1024*1024 or h[9]>storage.size:
        raise ValueError('invalid RomFS metadata bounds')
    dirs=storage.read(h[3],h[4]); files=storage.read(h[7],h[8]); seen_dirs=set(); seen_files=set()
    pending=[(0,'')]
    while pending:
        offset,parent=pending.pop()
        if offset==0xffffffff: continue
        if offset in seen_dirs: raise ValueError('cyclic RomFS directories')
        seen_dirs.add(offset); bounds(offset,24,len(dirs))
        _,sibling,child,file,_,length=struct.unpack_from('<6I',dirs,offset)
        bounds(offset+24,length,len(dirs))
        name=dirs[offset+24:offset+24+length].decode('utf-8')
        path='/'.join(x for x in (parent,name) if x)
        pending.append((sibling,parent)); pending.append((child,path))
        while file!=0xffffffff:
            if file in seen_files: raise ValueError('cyclic RomFS files')
            seen_files.add(file); bounds(file,32,len(files))
            _,following,data,size,_,length=struct.unpack_from('<IIQQII',files,file)
            bounds(file+32,length,len(files)); bounds(h[9]+data,size,storage.size)
            name=files[file+32:file+32+length].decode('utf-8')
            yield dict(name=path+'/'+name,offset=h[9]+data,size=size)
            file=following


class LogicalGames:
    def __init__(self,root):
        self.root=Path(root).resolve(); self.stack=ExitStack(); self.files={}; self.sections={}; self.logical={}
    def __enter__(self): return self
    def __exit__(self,*args): return self.stack.__exit__(*args)
    def physical(self,name):
        from nxshader import safe_file
        if name not in self.files:
            f=self.stack.enter_context(safe_file(self.root,name).open('rb'))
            mm=self.stack.enter_context(mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ))
            self.files[name]=Physical(mm,name)
            self.sections[name]=list(sections(self.files[name]))
        return self.files[name]
    def patched(self,base_file,patch_file,base_nca,patch_nca,index):
        identity=(base_file,patch_file,base_nca,patch_nca,index)
        if identity in self.logical: return self.logical[identity]
        self.physical(base_file); self.physical(patch_file)
        base=[s for s in self.sections[base_file] if s['name']==base_nca and s['index']==index]
        patch=[s for s in self.sections[patch_file] if s['name']==patch_nca and s['index']==index]
        if len(base)!=1 or len(patch)!=1: raise ValueError('base/patch section identity missing')
        base,patch=base[0],patch[0]
        if (base['program_id']&~0x800)!=(patch['program_id']&~0x800) or base['content_index']!=patch['content_index']:
            raise ValueError('base/update content identity mismatch')
        h=patch['header']
        if h[5]!=0 or struct.unpack_from('<Q',h,0x150)[0] or struct.unpack_from('<Q',h,0x180)[0]:
            raise ValueError('patch meta-hash/sparse/compression layer not supported by plaintext POC')
        entries,end,patch_data_size=bucket_entries(patch['storage'],h)
        logical=Indirect(base['storage'],Slice(patch['storage'],0,patch_data_size),entries,end)
        offset,size=romfs_range(h)
        result=Slice(logical,offset,size)
        self.logical[identity]=result
        return result
    def pairs(self,base_file,patch_file):
        self.physical(base_file); self.physical(patch_file)
        for patch in self.sections[patch_file]:
            if patch['content_type']!=0 or patch['header'][2]!=0 or not struct.unpack_from('<Q',patch['header'],0x108)[0]: continue
            candidates=[base for base in self.sections[base_file] if base['content_type']==patch['content_type']
                        and base['index']==patch['index'] and base['content_index']==patch['content_index']
                        and (base['program_id']&~0x800)==(patch['program_id']&~0x800)]
            if len(candidates)!=1: raise ValueError('ambiguous/missing original program section')
            base=candidates[0]
            yield base,patch,self.patched(base_file,patch_file,base['name'],patch['name'],patch['index'])


def resolve_references(profile,root,requests,library=None):
    from nxshader import sha
    groups={}
    for digest,ref in requests:
        identity=(ref['base_file'],ref['file'],ref['base_nca'],ref['patch_nca'],ref['section'],
                  ref['resource_offset'],ref['resource_size'],ref['encoding'])
        groups.setdefault(identity,[]).append((digest,ref))
    result={}
    with LogicalGames(root) as games:
        for identity,refs in groups.items():
            base_file,patch_file,base_nca,patch_nca,section,offset,size,encoding=identity
            if not 0<size<=512*1024*1024: raise ValueError('logical resource too large')
            storage=games.patched(base_file,patch_file,base_nca,patch_nca,section)
            resource=storage.read(offset,size)
            if any(sha(resource)!=ref['resource_sha256'] for _,ref in refs):
                raise ValueError('logical resource fingerprint mismatch')
            decoded=decode_yaz0(resource,library) if encoding=='bktr-yaz0' else resource
            for digest,ref in refs:
                length=profile['shaders'][digest]['size']; bounds(ref['offset'],length,len(decoded))
                if ref['size']!=length: raise ValueError('logical shader size mismatch')
                code=decoded[ref['offset']:ref['offset']+length]
                if sha(code)==digest: result[digest]=code
    return result
