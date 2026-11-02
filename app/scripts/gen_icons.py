"""
Make every app icon from the hand-drawn logo in assets/logo-drawing.png, so the iOS icon,
Android adaptive icon, favicon and splash all show the same drawing.

  python scripts/gen_icons.py      (needs Pillow)

The drawing is used as it is (not redrawn): its white strokes are cut out of the grey
background and placed, centred, on square canvases of the size each platform needs.
"""
import os

from PIL import Image, ImageChops

ASSETS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets")
SOURCE = os.path.join(ASSETS, "logo-drawing.png")
BG = (55, 55, 55)          # #373737, the drawing's background grey


def strokes():
    """The drawing's strokes as white with an alpha mask, cropped to the strokes."""
    gray = Image.open(SOURCE).convert("L")
    # background 55 -> alpha 0, white 255 -> alpha 255, keeping the soft edges
    alpha = gray.point(lambda v: max(0, min(255, (v - BG[0]) * 255 // (255 - BG[0]))))
    alpha = alpha.point(lambda a: 0 if a < 8 else a)        # drop faint noise
    box = alpha.getbbox()
    white = Image.new("RGBA", gray.size, (255, 255, 255, 0))
    white.putalpha(alpha)
    return white.crop(box)


def icon(size, fill, bg):
    """size px square; the drawing's longer side takes `fill` of the width."""
    art = strokes()
    s = fill * size / max(art.size)
    art = art.resize((round(art.width * s), round(art.height * s)), Image.LANCZOS)
    im = Image.new("RGBA", (size, size), bg + (255,) if bg else (0, 0, 0, 0))
    im.alpha_composite(art, ((size - art.width) // 2, (size - art.height) // 2))
    return im


def main():
    out = lambda name: os.path.join(ASSETS, name)
    icon(1024, 0.83, BG).convert("RGB").save(out("icon.png"))          # iOS: square, no transparency
    # Android adaptive: only the centre 66/108 is always visible after the launcher's mask
    icon(512, 0.44, None).save(out("android-icon-foreground.png"))
    Image.new("RGBA", (512, 512), BG + (255,)).save(out("android-icon-background.png"))
    icon(432, 0.44, None).save(out("android-icon-monochrome.png"))
    icon(48, 0.9, BG).save(out("favicon.png"))
    icon(1024, 0.5, None).save(out("splash-icon.png"))
    print("icons written to", os.path.abspath(ASSETS))


if __name__ == "__main__":
    main()
