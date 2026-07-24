#!/usr/bin/env python3
"""Convert any image (jpg/png/webp/...) to the binary PPM color_probe reads.

Kept deliberately dumb: the probe does the camera pipeline (centre-crop, scale to
96x96, RGB565), so this only decodes and hands over full-resolution RGB. That way
the C tool alone decides what the board sees, and the Python here can never
quietly change the answer.

    python3 tools/img2ppm.py photo.jpg /tmp/photo.ppm
"""

import sys

try:
    from PIL import Image
except ImportError:  # pragma: no cover - a helpful message beats a stack trace
    sys.exit("needs Pillow: pip install pillow  (or: apt install python3-pil)")


def main() -> None:
    if len(sys.argv) != 3:
        sys.exit(f"usage: {sys.argv[0]} <image> <out.ppm>")
    src, dst = sys.argv[1], sys.argv[2]
    # convert("RGB") also flattens transparency and drops EXIF colour profiles;
    # exif_transpose first so a phone photo is not probed sideways.
    from PIL import ImageOps

    img = ImageOps.exif_transpose(Image.open(src)).convert("RGB")
    img.save(dst, format="PPM")
    print(f"{src} {img.size[0]}x{img.size[1]} -> {dst}")


if __name__ == "__main__":
    main()
