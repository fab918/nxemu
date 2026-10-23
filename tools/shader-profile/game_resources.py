"""Read-only indexing of plaintext DNCA/PFS0/RomFS; no keys or decryption.

Offsets are physical offsets within the named local container. Patch virtual
RomFS (BKTR) is deliberately not interpreted as a contiguous physical file.
"""
import mmap
from pathlib import Path
import struct
import ctypes


def decode_yaz0(raw, library=None):
    if raw[:4] != b'Yaz0' or len(raw) < 16:
        raise ValueError('invalid Yaz0 header')
    expected, = struct.unpack_from('>I', raw, 4)
    if expected > 512 * 1024 * 1024:
        raise ValueError('decoded resource exceeds POC limit')
    if library:
        dll = ctypes.CDLL(str(Path(library).resolve()))
        func = dll.nxshader_yaz0
        func.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t]
        func.restype = ctypes.c_int
        output = ctypes.create_string_buffer(expected)
        if func(raw, len(raw), output, expected):
            raise ValueError('invalid Yaz0 stream')
        return output.raw
    output, p = bytearray(), 16
    while len(output) < expected:
        if p >= len(raw):
            raise ValueError('truncated Yaz0 stream')
        flags, p = raw[p], p+1
        for bit in range(8):
            if len(output) == expected:
                break
            if p >= len(raw):
                raise ValueError('truncated Yaz0 stream')
            if flags & (128 >> bit):
                output.append(raw[p])
                p += 1
            else:
                if p+2 > len(raw):
                    raise ValueError('truncated Yaz0 backreference')
                first, second = raw[p:p+2]
                p += 2
                distance, length = ((first & 15) << 8) + second + 1, first >> 4
                if length == 0:
                    if p >= len(raw):
                        raise ValueError('truncated Yaz0 length')
                    length, p = raw[p]+18, p+1
                else:
                    length += 2
                if distance > len(output) or len(output)+length > expected:
                    raise ValueError('invalid Yaz0 backreference bounds')
                for _ in range(length):
                    output.append(output[-distance])
    return bytes(output)


def pfs_entries(data, start=0):
    if data[start:start+4] != b'PFS0':
        raise ValueError('not PFS0')
    count, strings = struct.unpack_from('<II', data, start+4)
    if count > 65536 or strings > 16 * 1024 * 1024:
        raise ValueError('invalid PFS0 header')
    names = start+16+24*count
    base = names+strings
    for i in range(count):
        off, size, name, _ = struct.unpack_from('<QQII', data, start+16+i*24)
        end = data.find(b'\0', names+name, names+strings)
        if end < 0 or base+off+size > len(data):
            raise ValueError('invalid PFS0 entry')
        yield data[names+name:end].decode(), base+off, size


def romfs_entries(data, base, limit):
    h = struct.unpack_from('<10Q', data, base)
    if h[0] != 80 or any(base+h[i]+h[i+1] > limit for i in (1, 3, 5, 7)):
        raise ValueError('invalid RomFS bounds')
    dirs, files, payload = base+h[3], base+h[7], base+h[9]
    visited = set()

    def walk(offset, parent):
        while offset != 0xffffffff:
            if offset in visited or offset+24 > h[4]:
                raise ValueError('invalid/cyclic RomFS directory')
            visited.add(offset)
            _, sibling, child, file, _, length = struct.unpack_from('<6I', data, dirs+offset)
            name = data[dirs+offset+24:dirs+offset+24+length].decode()
            path = '/'.join(x for x in (parent, name) if x)
            seen = set()
            while file != 0xffffffff:
                if file in seen or file+32 > h[8]:
                    raise ValueError('invalid/cyclic RomFS file')
                seen.add(file)
                _, next_file, off, size, _, n = struct.unpack_from('<IIQQII', data, files+file)
                filename = data[files+file+32:files+file+32+n].decode()
                if payload+off+size > limit:
                    raise ValueError('RomFS file outside section')
                yield path+'/'+filename, payload+off, size
                file = next_file
            if child != 0xffffffff:
                yield from walk(child, path)
            offset = sibling
    yield from walk(0, '')


def resources(data):
    for nca_name, nca, nca_size in pfs_entries(data):
        if not nca_name.endswith('.nca'):
            continue
        if data[nca+0x200:nca+0x204] != b'DNCA':
            raise ValueError('only plaintext DNCA is supported')
        for index in range(4):
            sector, end = struct.unpack_from('<II', data, nca+0x240+index*16)
            if sector == end:
                continue
            fs = nca+0x400+index*512
            begin, limit = nca+sector*512, nca+end*512
            if limit > nca+nca_size:
                raise ValueError('NCA section outside container')
            # Do not mistake a BKTR virtual RomFS offset for a physical one.
            indirect_size = struct.unpack_from('<Q', data, fs+0x108)[0]
            if indirect_size:
                yield dict(name=nca_name+f'/section{index}/BKTR-unmapped', offset=begin,
                           size=limit-begin, kind='patch-unmapped')
                continue
            fs_type, hash_type = data[fs+2], data[fs+3]
            if hash_type == 3:
                levels, = struct.unpack_from('<I', data, fs+20)
                if not 2 <= levels <= 7:
                    raise ValueError('invalid IVFC levels')
                offset, size = struct.unpack_from('<QQ', data, fs+24+(levels-2)*24)
            elif hash_type == 2:
                layers, = struct.unpack_from('<I', data, fs+44)
                if not 1 <= layers <= 5:
                    raise ValueError('invalid hash layer count')
                offset, size = struct.unpack_from('<QQ', data, fs+48+(layers-1)*16)
            else:
                continue
            section = begin+offset
            if fs_type == 0:
                for name, off, length in romfs_entries(data, section, min(section+size, limit)):
                    yield dict(name=nca_name+'/romfs/'+name.lstrip('/'), offset=off, size=length, kind='romfs')
            elif fs_type == 1:
                for name, off, length in pfs_entries(data, section):
                    yield dict(name=nca_name+'/exefs/'+name, offset=off, size=length, kind='exefs')


if __name__ == '__main__':
    import collections
    import json
    import sys
    for arg in sys.argv[1:]:
        with open(arg, 'rb') as f, mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as data:
            entries = list(resources(data))
            print(Path(arg).name, len(entries), collections.Counter(Path(x['name']).suffix for x in entries))
            print(json.dumps([x for x in entries if any(k in x['name'].lower() for k in ('shader', '.szs', '.bfsha'))][:45], indent=2))
