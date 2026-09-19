"""Lossless canvas editor for the PC CUICanvasResource format.

Preserves every original record and opaque table; appends bounded text and native
pointer controls. Refuses unsupported trailing data instead of discarding it.
"""
import struct
import echovr_pkg as P


def u64(data, off):
    return struct.unpack_from('<Q', data, off)[0]


def descriptor(header, off, count, stride):
    struct.pack_into('<Q', header, off + 8, count * stride)
    struct.pack_into('<QQ', header, off + 40, count, count)


class Canvas:
    def __init__(self, data):
        if len(data) < 432:
            raise ValueError('Truncated canvas header')
        self.header = bytearray(data[:432])
        cursor = 432
        self.padding = {}

        def take(size):
            nonlocal cursor
            value = bytearray(data[cursor:cursor+size])
            cursor += size
            if len(value) != size:
                raise ValueError('Truncated canvas array')
            return value

        def block(key, size, alignment):
            if not size:
                return bytearray()
            self.padding[key] = take((-cursor) % alignment)
            return take(size)

        def array(off, stride, alignment):
            size = u64(data, off+48)*stride
            # Native ReadBytesSkip::Count is a mislabeled alignment accessor at
            # descriptor +0x18; the attach codec clamps it to the type alignment.
            return block(off, size, max(alignment,struct.unpack_from('<I',data,off+24)[0]))

        elements = array(0x50,224,8)
        self.elements = [elements[i:i+224] for i in range(0,len(elements),224)]
        behaviors = array(0x88,88,8)
        self.behaviors = [behaviors[i:i+88] for i in range(0,len(behaviors),88)]
        self.behavior_data = [block(('behavior',i),u64(r,0x18),max(1,struct.unpack_from('<I',r,0x28)[0])) for i,r in enumerate(self.behaviors)]
        self.atlas = array(0xc0,24,8)
        self.string_offsets = array(0x100,4,4)
        self.text = array(0x138,1,1)
        self.bindings = array(0x170,24,8)
        if cursor != len(data):
            raise ValueError(f'Unsupported tail: consumed {cursor} of {len(data)}')
        self.original = bytes(data)

    def serialize(self):
        result = bytearray(self.header)

        def block(key, payload, alignment):
            if not payload:
                return
            n = (-len(result)) % alignment
            old = self.padding.get(key,b'')
            result.extend(old if len(old)==n else bytes(n))
            result.extend(payload)

        def array(off,payload,alignment):
            block(off,payload,max(alignment,struct.unpack_from('<I',self.header,off+24)[0]))

        array(0x50,b''.join(self.elements),8)
        array(0x88,b''.join(self.behaviors),8)
        for i,(row,payload) in enumerate(zip(self.behaviors,self.behavior_data)):
            block(('behavior',i),payload,max(1,struct.unpack_from('<I',row,0x28)[0]))
        array(0xc0,self.atlas,8)
        array(0x100,self.string_offsets,4)
        array(0x138,self.text,1)
        array(0x170,self.bindings,8)
        return bytes(result)

    def append(self, template, name, rect, parent=-1, hidden=False):
        row = bytearray(template)
        index = len(self.elements)
        struct.pack_into('<Q', row, 0, P.sym(name))
        struct.pack_into('<I', row, 0x10, int(hidden))
        struct.pack_into('<4f', row, 0x24, 0, 0, 0, 0)
        struct.pack_into('<4f', row, 0x34, *rect)
        struct.pack_into('<iI', row, 0x5c, parent, index)
        self.elements.append(row)
        descriptor(self.header, 0x50, len(self.elements), 224)
        return index

    def label(self, template, name, text, rect, size=30, hidden=False, capacity=0):
        """`capacity` reserves room for longer strings written later at runtime.

        Native SetText writes into this slot, so anything the runtime may show
        must fit; the default only covers the literal given here.
        """
        index = self.append(template, name, rect, hidden=hidden)
        row = self.elements[index]
        encoded = text.encode('utf-8')
        capacity = max(64, len(encoded)+1, capacity)
        slot = len(self.string_offsets)//4
        self.string_offsets += struct.pack('<I', len(self.text))
        self.text += encoded + bytes(capacity-len(encoded))
        struct.pack_into('<II', row, 0x90, slot, capacity)
        struct.pack_into('<H', row, 0xa4, size)
        # The donor title's foreground is black and normally recolored by its
        # script. Our literal labels need their own readable foreground. +0xb0
        # is the secondary text color; native SetColor writes RGBA at +0x78.
        struct.pack_into('<4f', row, 0x78, 1, 1, 1, 1)
        # Static literal text: no localization/blackboard bindings.
        row[0xc0:0xe0] = b'\xff' * 32
        descriptor(self.header, 0x100, slot+1, 4)
        descriptor(self.header, 0x138, len(self.text), 1)
        return index

    def pointer(self, record, payload, index, name):
        row = bytearray(record)
        struct.pack_into('<Q', row, 8, P.sym(name))
        struct.pack_into('<Q', row, 0x48, index)
        self.behaviors.append(row)
        self.behavior_data.append(bytearray(payload))
        descriptor(self.header, 0x88, len(self.behaviors), 88)

    def validate(self):
        n = len(self.elements)
        for i, row in enumerate(self.elements):
            parent, index = struct.unpack_from('<iI', row, 0x5c)
            if index != i or parent < -1 or parent >= i:
                raise ValueError(f'Invalid hierarchy/id at element {i}')
            if struct.unpack_from('<I', row, 8)[0] == 1:
                slot, capacity = struct.unpack_from('<II', row, 0x90)
                if slot * 4 + 4 > len(self.string_offsets):
                    raise ValueError('Text slot outside offset array')
                start = struct.unpack_from('<I', self.string_offsets, slot*4)[0]
                if start+capacity > len(self.text):
                    raise ValueError('Text capacity outside buffer')
        for row, data in zip(self.behaviors, self.behavior_data):
            if u64(row, 0x48) >= n or u64(row, 0x18) != len(data):
                raise ValueError('Invalid behavior element/payload')
        if Canvas(self.serialize()).serialize() != self.serialize():
            raise ValueError('Round-trip mismatch')
