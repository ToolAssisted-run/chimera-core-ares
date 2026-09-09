#!/usr/bin/env python3
"""Compares a frame this core drew against the picture real hardware draws.

PeterLemon's test ROMs ship a .png of what each one looks like on a console.
Ours comes out as a .ppm from run-native's --dump-frame, at twice the size
(the rasteriser doubles a 240-line picture), so the comparison samples one of
each doubled pixel rather than scaling anything.

    compare-picture.py <expected.png> <got.ppm> [max mismatching pixels]

Exits 0 when the number of differing pixels is within the allowance.
"""
import struct
import sys
import zlib


def read_png(path):
    data = open(path, 'rb').read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError(f'{path} is not a PNG')
    pos, idat, palette = 8, b'', None
    width = height = colour_type = 0
    while pos < len(data):
        length, kind = struct.unpack('>I4s', data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b'IHDR':
            width, height, depth, colour_type = struct.unpack('>IIBB', chunk[:10])
            if depth != 8:
                raise ValueError(f'{path}: only 8 bits per channel is supported')
        elif kind == b'IDAT':
            idat += chunk
        elif kind == b'PLTE':
            palette = chunk
        elif kind == b'IEND':
            break

    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[colour_type]
    raw = zlib.decompress(idat)
    stride = width * channels
    out = bytearray()
    previous = bytearray(stride)
    at = 0
    for _ in range(height):
        filter_kind = raw[at]
        at += 1
        line = bytearray(raw[at:at + stride])
        at += stride
        for x in range(stride):
            a = line[x - channels] if x >= channels else 0
            b = previous[x]
            c = previous[x - channels] if x >= channels else 0
            if filter_kind == 1:
                line[x] = (line[x] + a) & 255
            elif filter_kind == 2:
                line[x] = (line[x] + b) & 255
            elif filter_kind == 3:
                line[x] = (line[x] + ((a + b) >> 1)) & 255
            elif filter_kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                nearest = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + nearest) & 255
        out += line
        previous = line

    pixels = []
    for i in range(width * height):
        if colour_type == 3:
            k = out[i] * 3
            pixels.append((palette[k], palette[k + 1], palette[k + 2]))
        elif colour_type == 2:
            pixels.append((out[i * 3], out[i * 3 + 1], out[i * 3 + 2]))
        elif colour_type == 6:
            pixels.append((out[i * 4], out[i * 4 + 1], out[i * 4 + 2]))
        else:
            v = out[i]
            pixels.append((v, v, v))
    return width, height, pixels


def read_ppm(path):
    data = open(path, 'rb').read()
    at = data.index(b'255\n') + 4
    header = data[:at].split()
    width, height = int(header[1]), int(header[2])
    body = data[at:]
    return width, height, [(body[k], body[k + 1], body[k + 2])
                           for k in range(0, width * height * 3, 3)]


def main():
    if len(sys.argv) < 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    allowed = int(sys.argv[3]) if len(sys.argv) > 3 else 0

    ew, eh, expected = read_png(sys.argv[1])
    gw, gh, got = read_ppm(sys.argv[2])
    if gw % ew or gh % eh:
        print(f'{gw}x{gh} is not a whole multiple of {ew}x{eh}', file=sys.stderr)
        return 1
    sx, sy = gw // ew, gh // eh

    bad = 0
    first = None
    for y in range(eh):
        for x in range(ew):
            e = expected[y * ew + x]
            g = got[(y * sy) * gw + x * sx]
            if e != g:
                bad += 1
                if first is None:
                    first = (x, y, e, g)

    total = ew * eh
    if bad > allowed:
        print(f'{bad}/{total} pixels differ (allowed {allowed}); '
              f'first at {first[0]},{first[1]}: expected {first[2]}, got {first[3]}',
              file=sys.stderr)
        return 1
    print(f'{total - bad}/{total} pixels identical to hardware '
          f'({ew}x{eh} reference, {gw}x{gh} frame)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
