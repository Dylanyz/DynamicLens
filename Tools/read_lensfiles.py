# Copyright 2026 Dylan G (Mad Rice). Licensed under the Apache License, Version 2.0.
# SPDX-License-Identifier: Apache-2.0
# The lens files this script reads belong to Andy Davis (Imagery for Media); see NOTICE and SOURCES.md.

"""Unreal ULensFile .uasset -> JSON, without opening the editor.

    python Tools/read_lensfiles.py <folder of lens sets> <out.json>

Andy Davis ships his measured lenses as UE Lens File assets (his Cinelens release, and the
per-lens sets before it). Reading them needs either a running editor or a package parser; this is
the parser, so the fits in Tools/data/raw can be regenerated from the originals on any machine.

It is deliberately narrow: uncooked UE 5.5 packages, tagged properties, one ULensFile export. It
is not a general .uasset reader and will not survive being pointed at arbitrary assets.

Validated by reproducing all 31 lenses already in Tools/data/raw/andy_davis_lensfiles.json exactly
- sensor, image size, model name, focus encoder flag, and every distortion parameter to within
1e-9.

The UE 5.5 property tag layout (FileVersionUE5 >= 1013, PROPERTY_TAG_COMPLETE_TYPE_NAME), worked
out by reading the bytes and confirmed by every property chain landing exactly on its terminating
'None':

    FName   Name                'None' ends the list
    TypeName                    recursive: FName + int32 ParamCount, depth first, e.g.
                                StructProperty<Vector2D</Script/CoreUObject>>
    int32   Size                bytes of value after the tag
    uint8   Flags               EPropertyTagFlags, see the constants below
    [int32  ArrayIndex]         Flags & 1
    [16     PropertyGuid]       Flags & 2
    [ext]                       Flags & 4

Two things in there cost real time and are worth keeping written down:

  * The byte after Size is a FLAGS field, not the old `bool HasPropertyGuid`. Treating it as a
    bool skips 16 bytes whenever a struct is natively serialised (Flags & 8) and derails the
    whole chain.
  * An array's payload is just int32 Count followed by the elements. There is no per-element
    tag - the complete type name already carries the element type. Elements are fixed width when
    natively serialised, otherwise each one ends on its own 'None'.
"""
import collections
import glob
import json
import os
import struct
import sys

# EPropertyTagFlags
HAS_ARRAY_INDEX = 1
HAS_GUID = 2
HAS_EXTENSIONS = 4
NATIVE_SERIALIZE = 8
BOOL_TRUE = 16

# structs written as raw bytes rather than tagged properties
NATIVE = {
    'Vector2D': ('<dd', ['X', 'Y']),
    'Vector2f': ('<ff', ['X', 'Y']),
    'IntPoint': ('<ii', ['X', 'Y']),
}
# ...and structs with a custom Serialize() that the tag does not always flag as native
NATIVE_NAMES = set(NATIVE) | {'RichCurveKey'}


class Package:
    """Just enough of FPackageFileSummary to find the name table and the main export."""

    def __init__(self, path):
        self.d = d = open(path, 'rb').read()
        if len(d) < 32 or struct.unpack_from('<I', d, 0)[0] != 0x9E2A83C1:
            raise ValueError(f'{path}: not an Unreal package')
        o = 4

        def i32():
            nonlocal o
            v = struct.unpack_from('<i', d, o)[0]
            o += 4
            return v

        def fstr():
            nonlocal o
            n = i32()
            if n == 0:
                return ''
            if n < 0:
                b = d[o:o - 2 * n]
                o += -2 * n
                return b.decode('utf-16le').rstrip('\0')
            b = d[o:o + n]
            o += n
            return b.decode('latin-1').rstrip('\0')

        legacy = i32()
        i32()                                   # LegacyUE3Version
        self.v4 = i32()
        self.v5 = i32() if legacy <= -8 else 0
        i32()                                   # FileVersionLicenseeUE4
        custom_versions = i32()                 # read first: i32() advances o, so `o += i32()*20`
        o += custom_versions * 20               # would add to a stale o (FGuid + int32 each)
        self.total = i32()
        fstr()                                  # package name
        i32()                                   # package flags
        name_count, name_off = i32(), i32()
        self.names = []
        keep, o = o, name_off
        for _ in range(name_count):
            self.names.append(fstr())
            o += 4                              # two uint16 hashes
        o = keep
        i32(), i32()                            # soft object paths
        fstr()                                  # localization id
        i32(), i32()                            # gatherable text
        i32()
        exp_off = i32()
        # The export map's exact layout moves between versions, so rather than parse it, scan it
        # for the (SerialSize, SerialOffset) int64 pair that lands inside the serial region. The
        # biggest one is the object we want; the others are AssetImportData and metadata.
        best, p = None, exp_off
        while p < len(d) - 16:
            size, off = struct.unpack_from('<qq', d, p)
            if 0 < size < len(d) and self.total <= off and off + size <= len(d):
                if best is None or size > best[0]:
                    best = (size, off)
            p += 4
        if best is None:
            raise ValueError(f'{path}: no export found')
        self.main = best

    def name(self, i):
        return self.names[i] if 0 <= i < len(self.names) else f'<{i}>'


class Reader:
    def __init__(self, pkg, o):
        self.p, self.d, self.o = pkg, pkg.d, o

    def i32(self):
        v = struct.unpack_from('<i', self.d, self.o)[0]
        self.o += 4
        return v

    def u8(self):
        v = self.d[self.o]
        self.o += 1
        return v

    def fname(self):
        i = self.i32()
        self.i32()                              # FName number
        return self.p.name(i)

    def fstr(self):
        n = self.i32()
        if n == 0:
            return ''
        if n < 0:
            b = self.d[self.o:self.o - 2 * n]
            self.o += -2 * n
            return b.decode('utf-16le').rstrip('\0')
        b = self.d[self.o:self.o + n]
        self.o += n
        return b.decode('latin-1').rstrip('\0')


def type_name(r, depth=0):
    if depth > 8:
        raise ValueError('type name nested too deep - reader is misaligned')
    n = r.fname()
    c = r.i32()
    if not 0 <= c <= 8:
        raise ValueError(f'implausible type parameter count {c} at {r.o} - reader is misaligned')
    return (n, [type_name(r, depth + 1) for _ in range(c)])


def read_tag(r):
    """-> (name, typename, size, flags, value_offset), or None at the terminating 'None'."""
    name = r.fname()
    if name == 'None':
        return None
    tn = type_name(r)
    size = r.i32()
    flags = r.u8()
    if flags & HAS_ARRAY_INDEX:
        r.i32()
    if flags & HAS_GUID:
        r.o += 16
    if flags & HAS_EXTENSIONS:
        if r.u8() & 1:
            r.u8()
    return name, tn, size, flags, r.o


def read_native(r, sname, size):
    spec = NATIVE.get(sname)
    if spec and struct.calcsize(spec[0]) == size:
        v = dict(zip(spec[1], struct.unpack_from(spec[0], r.d, r.o)))
    elif sname == 'RichCurveKey' and size >= 27:
        time, value = struct.unpack_from('<2f', r.d, r.o + 3)   # after three uint8 modes
        v = {'Time': time, 'Value': value}
    else:
        v = {'_native_bytes': size}
    r.o += size
    return v


def read_tagged(r, end):
    out = {}
    while r.o < end:
        t = read_tag(r)
        if t is None:
            break
        name, tn, size, flags, vo = t
        r.o = vo
        out[name] = read_value(r, tn, size, flags)
        r.o = vo + size
    return out


def read_struct(r, sname, size, flags=0):
    if flags & NATIVE_SERIALIZE or sname in NATIVE_NAMES:
        return read_native(r, sname, size)
    end = r.o + size
    v = read_tagged(r, end)
    r.o = end
    return v


def read_value(r, tn, size, flags=0):
    kind = tn[0]
    if kind == 'FloatProperty':
        return struct.unpack_from('<f', r.d, r.o)[0]
    if kind == 'DoubleProperty':
        return struct.unpack_from('<d', r.d, r.o)[0]
    if kind in ('IntProperty', 'Int32Property'):
        return struct.unpack_from('<i', r.d, r.o)[0]
    if kind == 'BoolProperty':
        return bool(flags & BOOL_TRUE)
    if kind == 'StrProperty':
        return r.fstr()
    if kind in ('NameProperty', 'ByteProperty', 'EnumProperty'):
        return r.fname()
    if kind == 'StructProperty':
        return read_struct(r, tn[1][0][0] if tn[1] else '?', size, flags)
    if kind == 'ArrayProperty':
        start = r.o
        inner = tn[1][0] if tn[1] else ('?', [])
        n = r.i32()
        if n == 0:
            return []
        end = start + size
        if inner[0] == 'FloatProperty':
            v = list(struct.unpack_from('<%df' % n, r.d, r.o))
            r.o += 4 * n
            return v
        if inner[0] == 'DoubleProperty':
            v = list(struct.unpack_from('<%dd' % n, r.d, r.o))
            r.o += 8 * n
            return v
        if inner[0] != 'StructProperty':
            return f'<array of {inner[0]} x{n}>'
        sname = inner[1][0][0] if inner[1] else '?'
        if flags & NATIVE_SERIALIZE or sname in NATIVE_NAMES:
            return [read_native(r, sname, (end - r.o) // n) for _ in range(n)]
        return [read_tagged(r, end) for _ in range(n)]
    return f'<{kind}>'


def read_lensfile(path):
    """The whole ULensFile export as nested dicts."""
    pkg = Package(path)
    size, off = pkg.main
    # the export's tagged properties start one byte in
    return read_tagged(Reader(pkg, off + 1), off + size)


# ------------------------------------------------------------------ the shape the fitters expect

def _rows(table, key, pick):
    out = []
    for fp in (table or {}).get('FocusPoints', []) or []:
        for zp in fp.get('ZoomPoints', []) or []:
            v = pick(zp)
            if v is not None:
                out.append({'focus': fp.get('Focus', 0.0), 'zoom': zp.get('Zoom', 0.0), key: v})
    return out


def _pair(zp, holder, field):
    h = zp.get(holder)
    if not h:
        return None
    p = h.get(field) or {}
    return [p.get('X'), p.get('Y')]


def extract(path):
    """One Lens File -> the andy_davis_lensfiles.json schema."""
    d = read_lensfile(path)
    info = d.get('LensInfo') or {}
    sensor = info.get('SensorDimensions') or {}
    image = info.get('ImageDimensions') or {}
    distortion = _rows(d.get('DistortionTable'), 'params',
                       lambda z: (z.get('DistortionInfo') or {}).get('Parameters'))
    # 5 parameters is Brown-Conrady (USphericalLensModel); 14 is 3DE4 Anamorphic Standard Degree 4
    # (UAnamorphicLensModel), which this plugin's parametric path does not evaluate yet.
    width = len(distortion[0]['params']) if distortion else 0
    encoders = d.get('EncodersTable') or {}
    return {
        'model': 'AnamorphicLensModel' if width > 5 else 'SphericalLensModel',
        'sensor_mm': [sensor.get('X'), sensor.get('Y')],
        'image_px': [image.get('X'), image.get('Y')],
        'squeeze': 1.0,
        'name_in_file': info.get('LensModelName', ''),
        'distortion': distortion,
        'focal_length': _rows(d.get('FocalLengthTable'), 'fxfy',
                              lambda z: _pair(z, 'FocalLengthInfo', 'FxFy')),
        'image_center': _rows(d.get('ImageCenterTable'), 'center',
                              lambda z: _pair(z, 'ImageCenterInfo', 'PrincipalPoint')),
        'has_focus_encoder': bool((encoders.get('Focus') or {}).get('Keys')),
    }


def extract_tree(root):
    """Every .uasset under root, one or two levels deep, keyed by asset name."""
    files = sorted(glob.glob(os.path.join(root, '*.uasset'))
                   + glob.glob(os.path.join(root, '*', '*.uasset')))
    out = {}
    for f in files:
        name = os.path.basename(f)[:-7]
        try:
            out[name] = extract(f)
        except Exception as e:
            out[name] = {'_error': f'{type(e).__name__}: {e}'}
    return out


def main(argv):
    if len(argv) != 3:
        raise SystemExit(__doc__)
    out = extract_tree(argv[1])
    if not out:
        raise SystemExit(f'no .uasset files under {argv[1]}')
    with open(argv[2], 'w') as f:
        json.dump(out, f, indent=1)
    failed = [k for k, v in out.items() if '_error' in v]
    empty = [k for k, v in out.items() if '_error' not in v and not v['distortion']]
    models = collections.Counter(v.get('model') for v in out.values()
                                 if '_error' not in v and v['distortion'])
    print(f'{len(out)} lens files -> {argv[2]}')
    print(f'  with distortion: {len(out) - len(failed) - len(empty)}  {dict(models)}')
    print(f'  no distortion table: {len(empty)}')
    print(f'  failed to read: {len(failed)}')
    for k in failed:
        print(f'    {k}: {out[k]["_error"]}')


if __name__ == '__main__':
    main(sys.argv)
