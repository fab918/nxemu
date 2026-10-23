"""POC benchmark adapter. Prefix matching itself lives in shader_profile_search.h.

This bridge is not an NXEmu runtime dependency. A future native exporter calls
PrefixSearch directly on its resource buffers.
"""
import ctypes
from pathlib import Path


class NativeSearch:
    def __init__(self, library, prefixes):
        self.handle = None
        if not library or not prefixes or any(len(p) != 16 for p in prefixes):
            return
        self.library = ctypes.CDLL(str(Path(library).resolve()))
        try:
            self.create = self.library.nxshader_search_create
            self.scan = self.library.nxshader_search_scan
            self.destroy = self.library.nxshader_search_destroy
        except AttributeError:
            return  # Older POC helpers remain usable with the reference scanner.
        self.create.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        self.create.restype = ctypes.c_void_p
        self.destroy.argtypes = [ctypes.c_void_p]
        self.destroy.restype = None
        self.scan.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                              ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_size_t),
                              ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
        self.scan.restype = ctypes.c_int
        self.handle = self.create(b''.join(sorted(prefixes)), len(prefixes))
        if not self.handle:
            raise RuntimeError('Cannot construct native prefix search')

    def close(self):
        if self.handle:
            self.destroy(self.handle)
            self.handle = None

    def __del__(self):
        self.close()

    def positions(self, data, chunk_size=64*1024*1024):
        if chunk_size < 16:
            raise ValueError('Search chunk must hold a full prefix')
        hits = (ctypes.c_size_t * 4096)()
        next_position = 0
        # bytes can be borrowed directly; mmap is copied in bounded chunks, with
        # fifteen lookahead bytes so a prefix crossing the boundary is retained.
        stride = max(len(data), 16) if isinstance(data, bytes) else chunk_size
        for base in range(0, len(data), stride):
            owned_end = min(len(data), base+stride)
            block = data if isinstance(data, bytes) else data[base:min(len(data), owned_end+15)]
            resume = ctypes.c_size_t(max(0, next_position-base))
            count = ctypes.c_size_t()
            while resume.value < len(block):
                if self.scan(self.handle, block, len(block), ctypes.byref(resume),
                             hits, len(hits), ctypes.byref(count)):
                    raise RuntimeError('Native prefix search failed')
                for i in range(count.value):
                    position = base+hits[i]
                    if position >= owned_end:
                        break
                    next_position = position+16
                    yield position
                if count.value and base+hits[count.value-1] >= owned_end:
                    break
