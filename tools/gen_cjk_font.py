#!/usr/bin/env python3
# ---------------------------------------------------------------------------
# gen_cjk_font.py -- build the song-title font that lives on the SD card
# ---------------------------------------------------------------------------
# TFT_eSPI's built-in fonts are ASCII-only, so a title like "01. 傳說" drew as
# "01. ". The player renders titles from a .vlw smooth font kept on the card at
# /.sys/cjk16.vlw -- not in flash, where 2 MB of glyphs would not fit.
#
# The font is only loaded at boot, BEFORE Bluetooth starts, to turn each
# title into a small bitmap cached in /.sys/t/. TFT_eSPI keeps ~12 bytes of
# metrics per glyph in RAM while a font is loaded, so the subset is sized for
# that moment: ~8,000 glyphs is ~100 KB, which fits in the ~180 KB free before
# Bluetooth and would not fit anywhere after it.
#
# Coverage: ASCII and Latin-1, general and CJK punctuation, full-width forms,
# Japanese kana, Big5 level 1 (the 5,401 common Traditional characters) and
# GB2312 level 1 (the 3,755 common Simplified ones) -- song titles mix both.
# Characters the face does not have are left out rather than shipped as boxes.
#
#   python3 tools/gen_cjk_font.py                 # writes sdcard/.sys/cjk16.vlw
#   python3 tools/gen_cjk_font.py --out PATH
#
# Then copy the file to the SD card as /.sys/cjk16.vlw.
#
# The .vlw container, as TFT_eSPI 2.5.43 parses it (see the weather clock's
# tools/gen_vlw.py for the full account): big-endian int32 throughout.
#   header 24 B:   glyphCount, version, fontSizePt, <ignored>, ascent, descent
#   metrics 28 B:  codepoint, height, width, xAdvance, dY, dX, <ignored>
#   bitmaps:       width*height bytes of 8-bit alpha, in metrics order
# Glyphs must be sorted by codepoint -- the library searches them linearly,
# but sorted output keeps the file diffable and deterministic.
# ---------------------------------------------------------------------------

import argparse
import os
import struct
import sys

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    sys.exit("Pillow is required:  python3 -m pip install Pillow")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Heiti TC first: it is what the weather clock's Chinese labels were baked
# from, and it carries both Traditional and Simplified forms.
FACES = [
    ("/System/Library/Fonts/STHeiti Medium.ttc", 0),
    ("/System/Library/Fonts/PingFang.ttc", 2),
    ("/Library/Fonts/NotoSansHK-Medium.otf", 0),
    ("/usr/share/fonts/opentype/noto/NotoSansCJKtc-Medium.otf", 0),
    ("C:/Windows/Fonts/msjh.ttc", 0),
]

PIXEL_SIZE = 16        # target: ink fits the 18 px title strip
TOFU_PROBE = "\ue123"  # a private-use codepoint no face assigns


def load_face():
    for path, index in FACES:
        if os.path.exists(path):
            try:
                return ImageFont.truetype(path, PIXEL_SIZE, index=index), path
            except OSError:
                continue
    sys.exit("No CJK font found. Edit FACES in this script.")


def decode_range(codec, lead_lo, lead_hi, trail_ranges):
    out = set()
    for lead in range(lead_lo, lead_hi + 1):
        for lo, hi in trail_ranges:
            for trail in range(lo, hi + 1):
                try:
                    out.add(bytes([lead, trail]).decode(codec))
                except UnicodeDecodeError:
                    pass
    return out


def charset():
    chars = set()
    for lo, hi in [(0x21, 0x7E), (0xA1, 0xFF), (0x2010, 0x2027), (0x2030, 0x205E),
                   (0x3000, 0x303F), (0x3041, 0x30FF), (0xFF01, 0xFF5E)]:
        chars.update(chr(c) for c in range(lo, hi + 1))
    # Big5 level 1: lead 0xA4-0xC6, trail 0x40-0x7E and 0xA1-0xFE.
    chars |= decode_range("big5", 0xA4, 0xC6, [(0x40, 0x7E), (0xA1, 0xFE)])
    # GB2312 level 1: lead 0xB0-0xD7, trail 0xA1-0xFE.
    chars |= decode_range("gb2312", 0xB0, 0xD7, [(0xA1, 0xFE)])
    return sorted(c for c in chars if len(c) == 1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(ROOT, "sdcard", ".sys", "cjk16.vlw"))
    args = ap.parse_args()

    font, path = load_face()
    tofu = font.getmask(TOFU_PROBE, mode="L")
    tofu_sig = (tofu.size, bytes(tofu))

    metrics, bitmaps = [], []
    skipped = 0
    max_ascent = max_descent = 0
    for ch in charset():
        mask = font.getmask(ch, mode="L")
        if (mask.size, bytes(mask)) == tofu_sig:
            skipped += 1
            continue
        x0, y0, x1, y1 = font.getbbox(ch, anchor="ls")
        w, h = x1 - x0, y1 - y0
        adv = int(round(font.getlength(ch)))
        if w <= 0 or h <= 0 or w > 255 or h > 255 or not -128 <= x0 <= 127:
            skipped += 1
            continue
        pad = max(8, h)
        img = Image.new("L", (w + 2 * pad, h + 2 * pad), 0)
        ImageDraw.Draw(img).text((pad - x0, pad - y0), ch, font=font, fill=255, anchor="ls")
        data = img.crop((pad, pad, pad + w, pad + h)).tobytes()
        dy = -y0
        max_ascent = max(max_ascent, dy)
        max_descent = max(max_descent, h - dy)
        metrics.append((ord(ch), h, w, adv, dy, x0))
        bitmaps.append(data)

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(struct.pack(">6i", len(metrics), 11, PIXEL_SIZE, 0, max_ascent, max_descent))
        for cp, h, w, adv, dy, dx in metrics:
            f.write(struct.pack(">7i", cp, h, w, adv, dy, dx, 0))
        for data in bitmaps:
            f.write(data)

    size = os.path.getsize(args.out)
    print(f"font      : {os.path.basename(path)} at {PIXEL_SIZE} px")
    print(f"glyphs    : {len(metrics)}  (skipped {skipped} the face lacks)")
    print(f"height    : ascent {max_ascent} + descent {max_descent} = {max_ascent + max_descent} px")
    print(f"RAM while loaded on the device: ~{len(metrics) * 12 // 1024} KB")
    print(f"wrote     : {args.out}  ({size / 1024 / 1024:.2f} MB)")
    print("copy it to the SD card as /.sys/cjk16.vlw")


if __name__ == "__main__":
    main()
