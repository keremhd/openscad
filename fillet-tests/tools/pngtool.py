#!/usr/bin/env python3
"""Report PNG dimensions, and join renders into side-by-side or grid figures.

Two jobs, both about comparing renders:

    ./pngtool.py dims a.png b.png              width x height of each
    ./pngtool.py row out.png a.png b.png ...    join left to right
    ./pngtool.py grid out.png a.png b.png -- c.png d.png     rows split on --

`dims` exists because --viewall fits the model to whatever canvas it is given, so
--imgsize is part of a figure rather than a detail of it: re-rendering a set at
one "documented" size letterboxes every figure that was composed at another, and
the only way to notice is to compare the pixel dimensions.

`row` and `grid` take a --key colour per column or row, drawn as a bar, so a
before/after pair reads without a caption. Stdlib only -- no PIL, no ImageMagick.
"""

import struct
import sys
import zlib

GAP = 12          # px of background between panels
BAR = 8           # px of key bar
BG = bytes([70, 70, 70])
KEYS = {
    'red': bytes([176, 58, 46]),
    'green': bytes([39, 120, 80]),
    'grey': bytes([120, 120, 120]),
    'none': None,
}


def read_png(path):
    """Return (width, height, channels, rows) with rows as un-filtered bytearrays."""
    data = open(path, 'rb').read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError(f'{path}: not a PNG')
    pos, idat, w, h, depth, colour = 8, b'', None, None, None, None
    while pos < len(data):
        length = struct.unpack('>I', data[pos:pos + 4])[0]
        kind = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + length]
        if kind == b'IHDR':
            w, h, depth, colour = struct.unpack('>IIBB', chunk[:10])
        elif kind == b'IDAT':
            idat += chunk
        pos += 12 + length
    if depth != 8 or colour not in (2, 6):
        raise ValueError(f'{path}: need 8-bit RGB or RGBA, got depth={depth} colour={colour}')

    nch = 3 if colour == 2 else 4
    raw = zlib.decompress(idat)
    stride = w * nch
    rows, prev, p = [], bytearray(stride), 0
    for _ in range(h):
        filt = raw[p]
        p += 1
        line = bytearray(raw[p:p + stride])
        p += stride
        if filt == 1:
            for i in range(nch, stride):
                line[i] = (line[i] + line[i - nch]) & 255
        elif filt == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif filt == 3:
            for i in range(stride):
                a = line[i - nch] if i >= nch else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 255
        elif filt == 4:
            for i in range(stride):
                a = line[i - nch] if i >= nch else 0
                b = prev[i]
                c = prev[i - nch] if i >= nch else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pred = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 255
        rows.append(line)
        prev = line
    return w, h, nch, rows


def write_png(path, width, height, rows):
    raw = b''.join(b'\x00' + bytes(r) for r in rows)

    def chunk(kind, payload):
        head = struct.pack('>I', len(payload)) + kind + payload
        return head + struct.pack('>I', zlib.crc32(kind + payload) & 0xffffffff)

    out = b'\x89PNG\r\n\x1a\n'
    out += chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
    out += chunk(b'IDAT', zlib.compress(raw, 9))
    out += chunk(b'IEND', b'')
    open(path, 'wb').write(out)


def _rgb(line, nch):
    return [line[i:i + 3] for i in range(0, len(line), nch)]


def make_row(paths, keys=None):
    """Join images left to right. Returns (width, rows). Heights must match."""
    imgs = [read_png(p) for p in paths]
    height = imgs[0][1]
    if any(i[1] != height for i in imgs):
        raise ValueError(f'heights differ: {[i[1] for i in imgs]}')
    width = sum(i[0] for i in imgs) + GAP * (len(imgs) - 1)

    rows = []
    for y in range(height):
        line = bytearray()
        for n, img in enumerate(imgs):
            if n:
                line += BG * GAP
            for px in _rgb(img[3][y], img[2]):
                line += px
        rows.append(line)

    if keys and any(keys):
        bar = bytearray()
        for n, img in enumerate(imgs):
            if n:
                bar += BG * GAP
            colour = KEYS.get(keys[n] if n < len(keys) else 'none')
            bar += (colour or BG) * img[0]
        rows += [bytearray(bar) for _ in range(BAR)]
    return width, rows


def main(argv):
    if not argv:
        print(__doc__)
        return 1
    cmd, rest = argv[0], argv[1:]
    keys = []
    if '--key' in rest:
        at = rest.index('--key')
        keys = rest[at + 1].split(',')
        rest = rest[:at] + rest[at + 2:]

    if cmd == 'dims':
        for p in rest:
            w, h, _, _ = read_png(p)
            print(f'{p.split("/")[-1]:32s} {w}x{h}')
        return 0

    if cmd == 'row':
        out, srcs = rest[0], rest[1:]
        width, rows = make_row(srcs, keys)
        write_png(out, width, len(rows), rows)
        print(f'wrote {out}  {width}x{len(rows)}')
        return 0

    if cmd == 'grid':
        out, srcs = rest[0], rest[1:]
        groups, cur = [], []
        for s in srcs:
            if s == '--':
                groups.append(cur)
                cur = []
            else:
                cur.append(s)
        groups.append(cur)
        built = [make_row(g, [keys[i]] * len(g) if i < len(keys) else None)
                 for i, g in enumerate(groups)]
        width = max(w for w, _ in built)
        rows = []
        for n, (w, r) in enumerate(built):
            if n:
                rows += [bytearray(BG * width) for _ in range(GAP)]
            rows += [line + BG * (width - w) for line in r]
        write_png(out, width, len(rows), rows)
        print(f'wrote {out}  {width}x{len(rows)}')
        return 0

    print(f'unknown command {cmd!r}')
    return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
