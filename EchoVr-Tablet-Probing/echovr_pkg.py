"""Echo VR package/manifest reader (win10 build, no shared/ store).

Layout per ECHOVR_PACKAGE_FORMAT.md; component-resource records per
ECHOVR_GRABBABLE_OBJECTS_NOTES.md section 2.  Needs `zstandard`.

    python echovr_pkg.py level mpl_lobby_b_arena                    # list a level's resources
    python echovr_pkg.py cr mpl_lobby_b_arena CR15TouchInteractCR   # dump component records
"""
import os
import struct
import sys
from pathlib import Path

import zstandard

_default_game = Path(r"C:\Program Files\Meta Horizon\Software\Software\ready-at-dawn-echo-arena")
if not _default_game.is_dir():
    _default_game = Path(r"C:\echovr\ready-at-dawn-echo-arena")
ROOT = Path(os.environ.get("ECHOVR_DATA", str(Path(os.environ.get("ECHOVR_GAME", str(_default_game))) / "_data/5932408047/rad15/win10")))

POLY = 0x95AC9329AC4BC9B5
M = (1 << 64) - 1
_T = []
for _i in range(256):
    _v = (POLY << 1) & M if _i & 0x80 else 0
    if _i & 0x40:
        _v = 0xBEF5B57AF4DC5ADF if _i & 0x80 else POLY
    for _bit in (0x20, 0x10, 8, 4, 2, 1):
        _v = ((_v * 2) ^ POLY) & M if _i & _bit else (_v * 2) & M
    _T.append((_v * 2) & M)


def sym(name: str, seed: int = M) -> int:
    """CSymbol64::Lookup @ 0x1400ce120 (ASCII tolower)."""
    v = seed
    for ch in name.encode():
        c = ch + 32 if 65 <= ch <= 90 else ch
        v = (_T[(v >> 56) & 0xFF] ^ c ^ ((v << 8) & M)) & M
    return v


def typesym(cls: str, gpu: bool = False) -> int:
    """Resource type symbol: sym("Win10"[GPU], seed=sym(ClassName))."""
    return sym("Win10GPU" if gpu else "Win10", seed=sym(cls))


def instance_phys_name(asset_sym: int) -> int:
    """CPhysicsResource name used by ncaPhysics flag 0x20 (instance-model physics)."""
    return sym("~instance_phys", seed=asset_sym)


def decompress(raw: bytes) -> bytes:
    """0x18-byte container: tag[4], align u32, usize u64, csize u64, payload."""
    tag = raw[:4]
    if tag == b"ZSTD":
        return zstandard.ZstdDecompressor().decompressobj().decompress(raw[0x18:])
    if tag == b"NONE":
        return raw[0x18:]
    if tag == b" LZ4":
        import lz4.block

        usize = struct.unpack_from("<Q", raw, 8)[0]
        return lz4.block.decompress(raw[0x18:], uncompressed_size=usize)
    return raw


class Manifest:
    def __init__(self, mid: str):
        self.mid = mid
        blob = decompress((ROOT / "manifests" / mid).read_bytes())

        def cnt(d):
            return struct.unpack_from("<Q", blob, d + 0x38)[0]

        na, nb, nc = cnt(0), cnt(0x40), cnt(0x80)
        off = 0xC0
        self.A = [struct.unpack_from("<QQQII", blob, off + i * 0x20) for i in range(na)]
        off += na * 0x20
        self.B = [struct.unpack_from("<QQQQQ", blob, off + i * 0x28) for i in range(nb)]
        off += nb * 0x28
        self.C = [struct.unpack_from("<IIII", blob, off + i * 0x10) for i in range(nc)]
        self._frames = {}

    def frame(self, idx: int) -> bytes:
        if idx not in self._frames:
            pkg, foff, cs, _us = self.C[idx]
            with open(ROOT / "packages" / f"{self.mid}_{pkg}", "rb") as f:
                f.seek(foff)
                data = f.read(cs)
            self._frames[idx] = zstandard.ZstdDecompressor().decompressobj().decompress(data)
        return self._frames[idx]

    def get(self, type_sym: int, name_sym: int):
        for t, n, loc, size, _ in self.A:
            if t == type_sym and n == name_sym:
                start = loc >> 32
                return self.frame(loc & 0xFFFFFFFF)[start : start + size]
        return None

    def by_name(self, name_sym: int):
        return [(t, loc, size) for t, n, loc, size, _ in self.A if n == name_sym]

    def by_type(self, type_sym: int):
        return [(n, loc, size) for t, n, loc, size, _ in self.A if t == type_sym]


def open_all():
    return [Manifest(p.name) for p in (ROOT / "manifests").iterdir()
            if p.is_file() and len(p.name) == 16 and all(c in "0123456789abcdefABCDEF" for c in p.name)]


def parse_cr(blob: bytes):
    """Fixed-stride component resource -> (stride, [record bytes]).

    Container-type CRs (CModelCR, CInstanceModelCR, CScriptCR) are struct-of-arrays
    and return None here.
    """
    total = struct.unpack_from("<Q", blob, 8)[0]
    count = struct.unpack_from("<Q", blob, 0x28)[0]
    if count == 0 or total % count or 0x38 + total != len(blob):
        return None
    stride = total // count
    return stride, [blob[0x38 + i * stride : 0x38 + (i + 1) * stride] for i in range(count)]


def record_header(rec: bytes):
    ctype, actor, actorid, numpooled, hflags, cflags = struct.unpack_from("<QQHHIQ", rec, 0)
    return dict(type=ctype, actor=actor, actorid=actorid, numpooled=numpooled,
                hdr_flags=hflags, flags=cflags)


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    ms = open_all()
    level = sym(argv[2])
    if argv[1] == "level":
        for m in ms:
            for t, loc, size in m.by_name(level):
                print(f"{m.mid} type {t:016x} size {size:9d} frame {loc & 0xffffffff}")
    elif argv[1] == "cr":
        ts = typesym(argv[3])
        for m in ms:
            blob = m.get(ts, level)
            if blob is None:
                continue
            r = parse_cr(blob)
            if r is None:
                print("container-type CR, not fixed stride")
                return 0
            stride, recs = r
            print(f"stride 0x{stride:x} records {len(recs)}")
            for rec in recs:
                h = record_header(rec)
                print(f"  actor {h['actor']:016x} flags {h['flags']:x} | {rec[0x20:].hex(' ')}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
