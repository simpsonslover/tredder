"""
Draw the Tredder logo (a T between two corner brackets) and write every app icon from it,
so the iOS icon, Android adaptive icon, favicon and splash all match.

  python scripts/gen_icons.py      (needs Pillow)

The logo is defined on a 100 x 100 grid; each image is drawn 4x larger and scaled down
for smooth edges.
"""
import os

from PIL import Image, ImageDraw

ASSETS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets")
BG = (55, 55, 55)          # #373737, the grey of the original drawing
FG = (255, 255, 255)
SS = 4                     # supersampling

W = 7.5                    # stroke width
# (x0, y0, x1, y1) rectangles on the 100-unit grid; the logo spans 16..84
SHAPES = [
    (16, 16, 49, 16 + W), (16, 16, 16 + W, 49),          # top-left bracket
    (51, 84 - W, 84, 84), (84 - W, 51, 84, 84),          # bottom-right bracket
    (33, 31, 67, 31 + W),                                # T bar
    (50 - W * 0.6, 31, 50 + W * 0.6, 69),                # T stem
]


def logo(size, scale=1.0, bg=BG, fg=FG):
    """size px square. scale shrinks the logo around the centre (1.0 = logo spans 16..84 %)."""
    n = size * SS
    im = Image.new("RGBA", (n, n), bg + (255,) if bg else (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    u = n / 100.0
    r = W * u * 0.18       # slightly rounded stroke corners
    for x0, y0, x1, y1 in SHAPES:
        box = [((c - 50) * scale + 50) * u for c in (x0, y0, x1, y1)]
        d.rounded_rectangle(box, radius=r * scale, fill=fg + (255,))
    return im.resize((size, size), Image.LANCZOS)


def main():
    out = lambda name: os.path.join(ASSETS, name)
    logo(1024).convert("RGB").save(out("icon.png"))                      # iOS: square, no transparency
    # Android adaptive: the launcher masks to a circle/squircle and only the centre 66/108
    # is always visible, so the logo is shrunk to fit inside that circle
    logo(512, scale=0.62, bg=None).save(out("android-icon-foreground.png"))
    Image.new("RGBA", (512, 512), BG + (255,)).save(out("android-icon-background.png"))
    logo(432, scale=0.62, bg=None, fg=(255, 255, 255)).save(out("android-icon-monochrome.png"))
    logo(48).save(out("favicon.png"))
    logo(1024, scale=0.6, bg=None).save(out("splash-icon.png"))
    print("icons written to", os.path.abspath(ASSETS))


if __name__ == "__main__":
    main()
