"""Generates every Audioslave icon asset from the master logo.

    python tools/make_icons.py [assets/logo-master.png]

Outputs
    assets/logo-master.png         (input) the official logo as delivered, RGBA
    assets/logo-source.png         master cropped to the artwork, square
    assets/logo.png, logo-256.png  README / documentation
    assets/tray/normal-N.png       tray + UI frames (N = 16 ... 64, 128, 256)
    assets/tray/paused-N.png       grey variant shown while monitoring is paused
    resources/logo.ico             executable / window / installer icon
    resources/logo-paused.ico

The artwork is used exactly as delivered: it is only cropped to its visible
bounds (plus a small, even margin) and scaled. Every frame is rendered
directly from the full-resolution master - never from a smaller frame - in
linear light with premultiplied alpha and a Lanczos filter; frames of 48 px
and less get a light unsharp mask to recover the edge contrast the filter
softens. The tray picks the frame that exactly matches the shell's icon size
for the current DPI (16 px at 100 %, 20 px at 125 %, 24 px at 150 %, 28 px at
175 %, 32 px at 200 % ...), so Windows never has to scale it, and the .ico
carries every size Explorer, the taskbar, shortcuts and Apps & Features ask
for.
"""

import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parent.parent
TRAY_SIZES = [16, 20, 24, 28, 32, 36, 40, 48, 56, 64, 128, 256]
ICO_SIZES = [16, 20, 24, 28, 32, 36, 40, 48, 56, 64, 96, 128, 256]
MARGIN = 0.02  # of the square side, on every edge


def square_master(path: Path) -> Image.Image:
    """The logo cropped to its visible pixels and centred on a square canvas."""
    rgba = Image.open(path).convert("RGBA")
    alpha = np.asarray(rgba.getchannel("A"))
    ys, xs = np.nonzero(alpha > 8)
    box = (int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1)
    art = rgba.crop(box)
    side = int(round(max(art.size) * (1.0 + 2.0 * MARGIN)))
    canvas = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    canvas.paste(art, ((side - art.size[0]) // 2, (side - art.size[1]) // 2))
    return canvas


def to_linear(c: np.ndarray) -> np.ndarray:
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def to_srgb(c: np.ndarray) -> np.ndarray:
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * np.power(np.clip(c, 0, None), 1 / 2.4) - 0.055)


def resize(master: Image.Image, size: int) -> Image.Image:
    """Linear-light, premultiplied, Lanczos resize straight from the master."""
    data = np.asarray(master).astype(np.float64) / 255.0
    a = data[..., 3]
    premultiplied = to_linear(data[..., :3]) * a[..., None]

    def scaled(channel: np.ndarray) -> np.ndarray:
        img = Image.fromarray(channel.astype(np.float32), "F")
        return np.asarray(img.resize((size, size), Image.Resampling.LANCZOS)).astype(np.float64)

    out_a = np.clip(scaled(a), 0.0, 1.0)
    out_rgb = np.dstack([scaled(premultiplied[..., i]) for i in range(3)])
    safe = np.where(out_a > 1e-6, out_a, 1.0)
    out_rgb = to_srgb(np.clip(out_rgb / safe[..., None], 0.0, 1.0))
    rgba = np.dstack([out_rgb, out_a]) * 255.0
    image = Image.fromarray(np.round(rgba).clip(0, 255).astype(np.uint8), "RGBA")
    if size <= 48:
        # Recover the detail the filter softens at tray sizes.
        amount = 80 if size <= 24 else 50
        sharpened = image.filter(ImageFilter.UnsharpMask(radius=0.6, percent=amount, threshold=1))
        sharpened.putalpha(image.getchannel("A"))
        image = sharpened
    return image


def paused_variant(master: Image.Image) -> Image.Image:
    """Grey, dimmed version of the logo (monitoring paused or stopped)."""
    data = np.asarray(master).astype(np.float32)
    luma = data[..., 0] * 0.299 + data[..., 1] * 0.587 + data[..., 2] * 0.114
    grey = np.clip(luma * 0.85 + 18.0, 0, 255)
    out = np.dstack([grey, grey, grey, data[..., 3]]).astype(np.uint8)
    return Image.fromarray(out, "RGBA")


def main() -> None:
    source = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "assets" / "logo-master.png"
    master = square_master(source)
    paused = paused_variant(master)

    assets = ROOT / "assets"
    tray = assets / "tray"
    tray.mkdir(parents=True, exist_ok=True)
    for old in tray.glob("*.png"):
        old.unlink()

    master.save(assets / "logo-source.png")
    resize(master, 512).save(assets / "logo.png")
    resize(master, 256).save(assets / "logo-256.png")

    normal_frames, paused_frames = {}, {}
    for size in sorted(set(TRAY_SIZES + ICO_SIZES)):
        normal_frames[size] = resize(master, size)
        paused_frames[size] = resize(paused, size)
        if size in TRAY_SIZES:
            normal_frames[size].save(tray / f"normal-{size}.png")
            paused_frames[size].save(tray / f"paused-{size}.png")

    resources = ROOT / "resources"
    for name, frames in (("logo.ico", normal_frames), ("logo-paused.ico", paused_frames)):
        largest = frames[256]
        others = [frames[s] for s in ICO_SIZES if s != 256]
        largest.save(resources / name, format="ICO", sizes=[(s, s) for s in ICO_SIZES], append_images=others)

    print("assets regenerated from", source)


if __name__ == "__main__":
    main()
