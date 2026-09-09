#!/usr/bin/env python3
"""Reads what a test ROM wrote on the screen.

Test ROMs talk by drawing. jsmolka's ARM suite prints "All tests passed", or the
name of the test that failed, and the only way to hear it is to read the
picture. That is also what makes the leg honest: it cannot pass by agreeing with
another run of itself, the way a digest comparison can.

The font is 8x8, dark on light, and the glyphs below were lifted from a frame
this core drew and read by eye. A message made of letters that are not in the
table still comes out, with '?' where a letter is unknown - which is enough to
see what went wrong.

    read-verdict.py <frame.ppm> [words that must appear...]

Prints what it read. Exits non-zero if the words are missing, or if the ROM drew
nothing at all.
"""
import sys

CELL = 8

FONT = {
    "0111100011001100110011001111110011001100110011001100110000000000": "A",
    "0000000000000000011110000000110001111100110011000111110000000000": "a",
    "0000110000001100011111001100110011001100110011000111110000000000": "d",
    "0000000000000000011110001100110011111100110000000111100000000000": "e",
    "0011000000110000001100000011000000110000001100000001100000000000": "l",
    "0000000000000000111110001100110011001100111110001100000011000000": "p",
    "0000000000000000011110001100000001111000000011001111100000000000": "s",
    "0110000001100000111110000110000001100000011000000011100000000000": "t",
}


def read_ppm(path):
    data = open(path, "rb").read()
    at = data.index(b"255\n") + 4
    header = data[:at].split()
    w, h = int(header[1]), int(header[2])
    body = data[at:]
    dark = [[sum(body[(y * w + x) * 3:(y * w + x) * 3 + 3]) < 200
             for x in range(w)] for y in range(h)]
    return w, h, dark


def main():
    if len(sys.argv) < 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    expected = [word.lower() for word in sys.argv[2:]] or ["passed"]

    w, h, dark = read_ppm(sys.argv[1])
    rows = [y for y in range(h) if any(dark[y])]
    if not rows:
        print("the screen is blank - the ROM drew nothing", file=sys.stderr)
        return 1
    cols = [x for x in range(w) if any(dark[y][x] for y in rows)]

    # The text sits on an 8x8 grid; find the cell the first ink is in.
    top, left = min(rows), min(cols)
    message = []
    x = left
    while x + CELL <= w and top + CELL <= h:
        signature = "".join(
            "1" if dark[top + dy][x + dx] else "0"
            for dy in range(CELL) for dx in range(CELL))
        if "1" not in signature:
            message.append(" ")
        else:
            message.append(FONT.get(signature, "?"))
        x += CELL
    text = "".join(message).strip()

    missing = [word for word in expected if word not in text.lower()]
    if missing:
        print(f"the screen reads {text!r}, which does not contain {missing}", file=sys.stderr)
        return 1
    print(repr(text))
    return 0


if __name__ == "__main__":
    sys.exit(main())
