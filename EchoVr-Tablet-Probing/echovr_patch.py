"""Echo VR client-side data patcher: override / add resources by rewriting the manifest
and appending one new package file, without touching the stock package files.

    python echovr_patch.py info                       # header + package summary
    python echovr_patch.py build <patch.json> <out>   # write out/manifests/<id> + out/packages/<id>_N
    python echovr_patch.py verify <out>               # re-read the patched manifest, compare bytes
    python echovr_patch.py install <out>              # copy into the game dir (backs up the manifest)
    python echovr_patch.py uninstall                  # restore the backed-up manifest, remove added pkg
    python echovr_patch.py bundle <out> <dist> [dll...] # portable folder + install.ps1/uninstall.ps1 for another PC

patch.json:  {"override": [{"type": "CGTextureResource", "gpu": true, "name": "0x<sym>|string",
                             "file": "path"}, ...],
              "add": [ same shape ], "copy": [{"type":..., "gpu":..., "name": "...", "from": "..."}]}
"copy" points name at another existing resource's bytes (useful for swap tests).

Manifest layout (decoded 2026-09-11 from manifests/48037dc70b0ecab2 on this PC, see
ECHOVR_PACKAGE_FORMAT.md and ECHOVR_NOTES_CORPUS_DIGEST.md):
  container: "ZSTD" u32 0x10 u64 usize u64 csize, zstd payload at +0x18
  payload:   u32 numPackages | u32 maxBlock (0x80000) | 3 x (RadArrayDescriptor56 + u64 0)
             A[n] 0x20: u64 type, u64 name, u64 loc(frame | off<<32), u32 size, u32 align
             B[n] 0x28: u64 type, u64 name, u64 hash_lo, u64 hash_hi, u64 buildstamp
             C[m] 0x10: u32 pkg, u32 fileOffset, u32 csize, u32 usize  (+ one end marker per pkg, usize=0)
  A/B sorted by SIGNED (type, name).  Descriptor: +8 byteSize, +0x1c flags=1, +0x20 0x20, +0x28 cap, +0x30 count.
"""
import hashlib
import json
import shutil
import struct
import sys
from pathlib import Path

import zstandard

import echovr_pkg as P

MID = "48037dc70b0ecab2"
GAME_MANIFEST = P.ROOT / "manifests" / MID
GAME_PACKAGES = P.ROOT / "packages"
BACKUP = P.ROOT / "manifests" / (MID + ".orig")
FRAME_LIMIT = 0x800000  # stock frames go up to 78 MB uncompressed; keep ours modest


def s64(x):
    return x - (1 << 64) if x >= (1 << 63) else x


class ManifestFile:
    def __init__(self, raw: bytes):
        self.container_hdr = raw[:0x18]
        blob = P.decompress(raw)
        self.npkg, self.maxblock = struct.unpack_from("<II", blob, 0)
        na, nb, nc = (struct.unpack_from("<Q", blob, d + 0x30)[0] for d in (8, 0x48, 0x88))
        off = 0xC0
        self.A = [list(struct.unpack_from("<QQQII", blob, off + i * 0x20)) for i in range(na)]
        off += na * 0x20
        self.B = [list(struct.unpack_from("<QQQQQ", blob, off + i * 0x28)) for i in range(nb)]
        off += nb * 0x28
        self.C = [list(struct.unpack_from("<IIII", blob, off + i * 0x10)) for i in range(nc)]
        assert off + nc * 0x10 == len(blob), "manifest payload size mismatch"
        self.index = {(a[0], a[1]): i for i, a in enumerate(self.A)}

    @staticmethod
    def desc(size, count):
        """RadArrayDescriptor56: ptr, byteSize, alloc, pad, flags=1, p_base=0x20, capacity, count."""
        return struct.pack("<QQQIIQQQ", 0, size, 0, 0, 1, 0x20, count, count)

    def serialize(self) -> bytes:
        order = sorted(range(len(self.A)), key=lambda i: (s64(self.A[i][0]), s64(self.A[i][1])))
        A = [self.A[i] for i in order]
        B = [self.B[i] for i in order]
        body = bytearray(struct.pack("<II", self.npkg, self.maxblock))
        pad = struct.pack("<Q", 0)   # each array = descriptor + u64 (runtime pointer slot), none after the last
        body += self.desc(len(A) * 0x20, len(A)) + pad + self.desc(len(B) * 0x28, len(B)) + pad + self.desc(len(self.C) * 0x10, len(self.C))
        assert len(body) == 0xC0
        for a in A:
            body += struct.pack("<QQQII", *a)
        for b in B:
            body += struct.pack("<QQQQQ", *b)
        for c in self.C:
            body += struct.pack("<IIII", *c)
        payload = zstandard.ZstdCompressor(level=3).compress(bytes(body))
        return b"ZSTD" + struct.pack("<IQQ", 0x10, len(body), len(payload)) + payload


class Patcher:
    def __init__(self):
        self.mf = ManifestFile(GAME_MANIFEST.read_bytes())
        self.stock = P.Manifest(MID)
        self.items = []  # (type_sym, name_sym, data)

    @staticmethod
    def _sym(s, seed=None):
        return int(s, 16) if isinstance(s, str) and s.startswith("0x") else P.sym(s)

    def resolve_type(self, cls, gpu):
        return P.typesym(cls, bool(gpu))

    def add_item(self, cls, gpu, name, data: bytes):
        self.items.append((self.resolve_type(cls, gpu), self._sym(name), bytes(data)))

    def load_json(self, path):
        spec = json.loads(Path(path).read_text())
        for e in spec.get("override", []) + spec.get("add", []):
            self.add_item(e["type"], e.get("gpu", False), e["name"], Path(e["file"]).read_bytes())
        for e in spec.get("copy", []):
            src = self.stock.get(self.resolve_type(e["type"], e.get("gpu", False)), self._sym(e["from"]))
            if src is None:
                raise SystemExit(f"copy source not found: {e}")
            self.add_item(e["type"], e.get("gpu", False), e["name"], src)

    def build(self, out: Path):
        out = Path(out)
        (out / "manifests").mkdir(parents=True, exist_ok=True)
        (out / "packages").mkdir(parents=True, exist_ok=True)
        mf = self.mf
        pkg_idx = mf.npkg               # new package file index
        mf.npkg += 1
        # drop the end marker if a previous build of ours is present (fresh manifest read each run, so no)
        pkg = bytearray()
        cctx = zstandard.ZstdCompressor(level=3)
        stamp = mf.B[0][4]
        for t, n, data in self.items:
            if len(data) > FRAME_LIMIT:
                raise SystemExit(f"resource {t:016x}:{n:016x} is {len(data)} bytes; split not implemented")
            frame = cctx.compress(data)
            frame_idx = len(mf.C)
            mf.C.append([pkg_idx, len(pkg), len(frame), len(data)])
            pkg += frame
            loc = frame_idx | (0 << 32)
            h = hashlib.blake2b(data, digest_size=16).digest()
            a = [t, n, loc, len(data), 0x10]
            b = [t, n, struct.unpack_from("<Q", h, 0)[0], struct.unpack_from("<Q", h, 8)[0], stamp]
            key = (t, n)
            if key in mf.index:
                i = mf.index[key]
                mf.A[i] = a
                mf.B[i] = b
                print(f"override {t:016x}:{n:016x} {len(data)} B -> frame {frame_idx}")
            else:
                mf.index[key] = len(mf.A)
                mf.A.append(a)
                mf.B.append(b)
                print(f"add      {t:016x}:{n:016x} {len(data)} B -> frame {frame_idx}")
        mf.C.append([pkg_idx, len(pkg), 0, 0])  # end marker like the stock packages
        (out / "packages" / f"{MID}_{pkg_idx}").write_bytes(bytes(pkg))
        (out / "manifests" / MID).write_bytes(mf.serialize())
        (out / "patch_items.json").write_text(json.dumps(
            [{"type": f"{t:016x}", "name": f"{n:016x}", "size": len(d),
              "sha1": hashlib.sha1(d).hexdigest()} for t, n, d in self.items], indent=1))
        print(f"wrote {out/'manifests'/MID} and {out/'packages'}/{MID}_{pkg_idx} ({len(pkg)} B)")


def verify(out: Path):
    out = Path(out)
    items = json.loads((out / "patch_items.json").read_text())
    patched = P.Manifest.__new__(P.Manifest)
    patched.mid = MID
    blob = P.decompress((out / "manifests" / MID).read_bytes())
    mf_new = ManifestFile((out / "manifests" / MID).read_bytes())
    patched.A = [tuple(a) for a in mf_new.A]
    patched.B = [tuple(b) for b in mf_new.B]
    patched.C = [tuple(c) for c in mf_new.C]
    patched._frames = {}
    root_pkgs = GAME_PACKAGES

    def frame(idx):
        pkg, foff, cs, _ = patched.C[idx]
        p = out / "packages" / f"{MID}_{pkg}"
        if not p.exists():
            p = root_pkgs / f"{MID}_{pkg}"
        with open(p, "rb") as f:
            f.seek(foff)
            return zstandard.ZstdDecompressor().decompressobj().decompress(f.read(cs))
    patched.frame = frame
    ok = True
    for it in items:
        data = patched.get(int(it["type"], 16), int(it["name"], 16))
        good = data is not None and hashlib.sha1(data).hexdigest() == it["sha1"]
        ok &= good
        print(("OK  " if good else "BAD ") + f"{it['type']}:{it['name']} {it['size']} B")
    # spot-check untouched resources against the stock manifest
    stock = P.Manifest(MID)
    import random
    random.seed(1)
    keys = random.sample(stock.A, 40)
    for t, n, loc, size, _ in keys:
        if any(int(i["type"], 16) == t and int(i["name"], 16) == n for i in items):
            continue
        if stock.get(t, n) != patched.get(t, n):
            ok = False
            print(f"BAD  untouched resource differs: {t:016x}:{n:016x}")
    print("stock resources spot-check:", "OK" if ok else "FAILED")
    print("manifest header:", struct.unpack_from("<II", blob, 0), "entries", len(mf_new.A), "frames", len(mf_new.C))
    return ok


def bundle(out: Path, dist: Path, scripts=()):
    """Copy a built patch into a self-contained folder with PowerShell install/uninstall scripts."""
    out, dist = Path(out), Path(dist)
    (dist / "manifests").mkdir(parents=True, exist_ok=True)
    (dist / "packages").mkdir(parents=True, exist_ok=True)
    shutil.copy2(out / "manifests" / MID, dist / "manifests" / MID)
    for p in (out / "packages").iterdir():
        shutil.copy2(p, dist / "packages" / p.name)
    if (out / "patch_items.json").exists():
        shutil.copy2(out / "patch_items.json", dist / "patch_items.json")
    for sp in scripts:
        (dist / "scripts").mkdir(exist_ok=True)
        shutil.copy2(sp, dist / "scripts" / Path(sp).name)
    tpl = Path(__file__).resolve().parent / "patch_templates"
    shutil.copy2(tpl / "install.ps1", dist / "install.ps1")
    shutil.copy2(tpl / "uninstall.ps1", dist / "uninstall.ps1")
    print("bundle written to", dist)


def install(out: Path):
    out = Path(out)
    if not BACKUP.exists():
        shutil.copy2(GAME_MANIFEST, BACKUP)
        print("backed up stock manifest to", BACKUP)
    for p in (out / "packages").iterdir():
        shutil.copy2(p, GAME_PACKAGES / p.name)
        print("installed", GAME_PACKAGES / p.name)
    shutil.copy2(out / "manifests" / MID, GAME_MANIFEST)
    print("installed", GAME_MANIFEST)


def uninstall():
    if not BACKUP.exists():
        raise SystemExit("no backup manifest found; nothing to restore")
    shutil.copy2(BACKUP, GAME_MANIFEST)
    print("restored", GAME_MANIFEST)
    stock_n = ManifestFile(BACKUP.read_bytes()).npkg
    for p in GAME_PACKAGES.glob(f"{MID}_*"):
        idx = int(p.name.split("_")[-1])
        if idx >= stock_n:
            p.unlink()
            print("removed", p)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    cmd = argv[1]
    if cmd == "info":
        mf = ManifestFile(GAME_MANIFEST.read_bytes())
        print(f"packages {mf.npkg} maxblock 0x{mf.maxblock:x} entries {len(mf.A)} frames {len(mf.C)}")
        rt = ManifestFile(mf.serialize())
        print("re-serialize round trip:", rt.A == mf.A and rt.B == mf.B and rt.C == mf.C and rt.npkg == mf.npkg)
    elif cmd == "build":
        p = Patcher()
        p.load_json(argv[2])
        p.build(Path(argv[3]))
    elif cmd == "verify":
        return 0 if verify(Path(argv[2])) else 1
    elif cmd == "install":
        install(Path(argv[2]))
    elif cmd == "uninstall":
        uninstall()
    elif cmd == "bundle":
        bundle(Path(argv[2]), Path(argv[3]), argv[4:])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
