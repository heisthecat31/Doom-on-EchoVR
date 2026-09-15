"""Pin the installed PersonalDiscEverywhere v3.0.1 pair; no DLL writes."""
from pe_image import PEImage
import echovr_pkg as P

SPECS = {
    '351c49438bd38225.dll': dict(
        sha256='5421de8a34703d9872df0fa3841443eee8a28577fb50461cc5c01e2911bcd911',
        allowed=0xd000, update=0x3250),
    '38965d90a823f03f.dll': dict(
        sha256='deca1e86cd437116a1afab07bcf228274d9d51274b385a92ab7568d346759c75',
        allowed=0x20000),
}


def configuration():
    result = {}
    for name, spec in SPECS.items():
        pe = PEImage(P.ROOT.parents[3] / 'bin/win10/scripts' / name)
        if pe.sha256 != spec['sha256']:
            raise ValueError(f'Personal-disc binding requires the verified v3.0.1 pair: {name} differs')
        result[name] = dict(size=pe.size, sha256=pe.sha256, addresses={
            key: dict(rva=rva, bytes=pe.read(rva,16).hex())
            for key,rva in spec.items() if key != 'sha256'
        })
    return result
