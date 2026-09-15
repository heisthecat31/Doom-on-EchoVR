"""Small, dependency-free PE reader for pinning reverse-engineered addresses."""
import hashlib
import struct
from pathlib import Path


class PEImage:
    def __init__(self, path):
        self.path = Path(path)
        self.data = self.path.read_bytes()
        pe = struct.unpack_from('<I', self.data, 0x3c)[0]
        if self.data[pe:pe+4] != b'PE\0\0':
            raise ValueError('Not a PE image')
        machine, count = struct.unpack_from('<HH', self.data, pe+4)
        if machine != 0x8664:
            raise ValueError('Expected x64')
        optional = pe+24
        self.base = struct.unpack_from('<Q', self.data, optional+24)[0]
        self.size = struct.unpack_from('<I', self.data, optional+56)[0]
        start = optional+struct.unpack_from('<H', self.data, pe+20)[0]
        self.sections = []
        for i in range(count):
            row = start+i*40
            vs, va, rs, raw = struct.unpack_from('<4I', self.data, row+8)
            self.sections.append((va, vs, raw, rs))

    def read(self, rva, size):
        for va, vs, raw, rs in self.sections:
            if va <= rva and rva+size <= va+rs:
                return self.data[raw+rva-va:raw+rva-va+size]
        raise ValueError(f'RVA {rva:x} is outside file-backed sections')

    @property
    def sha256(self):
        return hashlib.sha256(self.data).hexdigest()
