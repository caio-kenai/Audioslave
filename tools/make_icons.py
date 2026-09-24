"""Generates every Audioslave icon asset from the master logo.

    python tools/make_icons.py [assets/logo-master.png]

Outputs
    assets/logo-master.png         (input) the logo as delivered, RGB
    assets/logo-source.png         master, outside of the rounded square made transparent
    assets/logo.png, logo-256.png  README / documentation
    assets/tray/normal-N.png       tray + UI frames (N = 16 ... 64, 128, 256)
    assets/tray/paused-N.png       grey variant shown while monitoring is paused
    resources/logo.ico             executable / window icon (all frames)
    resources/logo-paused.ico

Quality: every frame is rendered directly from the master (never from a
smaller frame) in linear light with premultiplied alpha and a Lanczos filter.
Frames of 48 px and less are composed at their own size ("hinted"): a
pixel-aligned orange ring, a tighter margin and the emblem as large as the
frame allows, so the eagle stays legible where the full artwork would turn
into a blur. The tray picks the frame that
exactly matches the shell's icon size for the current DPI (16 px at 100 %,
20 px at 125 %, 24 px at 150 %, 28 px at 175 %, 32 px at 200 % ...), so
Windows never has to scale it.
"""

import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parent.parent
TRAY_SIZES = [16, 20, 24, 28, 32, 36, 40, 48, 56, 64, 128, 256]
ICO_SIZES = [16, 20, 24, 28, 32, 36, 40, 48, 56, 64, 96, 128, 256]


def master_with_alpha(path: Path) -> Image.Image:
    """Makes everything outside the logo's rounded square transparent."""
    rgb = Image.open(path).convert("RGB")
    w, h = rgb.size
    marker = (0, 255, 0)
    probe = rgb.copy()
    for corner in [(0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1)]:
        if probe.getpixel(corner) != marker:
            ImageDraw.floodfill(probe, corner, marker, thresh=40)
    outside = np.all(np.asarray(probe) == marker, axis=-1)

    pixels = np.asarray(rgb).astype(np.float32)
    alpha = np.where(outside, 0.0, 255.0)

    # Anti-aliased edge: pixels next to the outside region are a blend of the
    # border colour and black; turn that blend into coverage instead of a dark
    # fringe.
    near = outside.copy()
    for _ in range(3):
        grown = near.copy()
        grown[1:, :] |= near[:-1, :]
        grown[:-1, :] |= near[1:, :]
        grown[:, 1:] |= near[:, :-1]
        grown[:, :-1] |= near[:, 1:]
        near = grown
    edge = near & ~outside
    border = np.array([254.0, 105.0, 2.0], dtype=np.float32)
    coverage = np.clip(pixels[..., 0] / border[0], 0.0, 1.0)
    alpha = np.where(edge, coverage * 255.0, alpha)
    pixels = np.where(edge[..., None], border, pixels)

    rgba = np.dstack([pixels, alpha]).clip(0, 255).astype(np.uint8)
    return Image.fromarray(rgba, "RGBA")


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
        amount = 90 if size <= 24 else 60
        sharpened = image.filter(ImageFilter.UnsharpMask(radius=0.6, percent=amount, threshold=1))
        sharpened.putalpha(image.getchannel("A"))
        image = sharpened
    return image


ORANGE = (254, 105, 2)


def emblem_mask(source: Path) -> Image.Image:
    """Orange emblem (eagle + wordmark + badge) as a coverage mask, without
    the rounded-square ring, cropped to its bounding box."""
    rgb = Image.open(source).convert("RGB")
    data = np.asarray(rgb).astype(np.float32)
    h, w, _ = data.shape
    coverage = np.clip(data[..., 0] / ORANGE[0], 0.0, 1.0)

    # The ring is the orange component that touches the top-centre border.
    # .copy(): fromarray may wrap the numpy buffer read-only, and floodfill
    # would then silently change nothing.
    solid = Image.fromarray(((coverage > 0.35) * 255).astype(np.uint8), "L").copy()
    ys = np.nonzero(np.asarray(solid)[:, w // 2] > 0)[0]
    ImageDraw.floodfill(solid, (w // 2, int(ys[0])), 128)
    ring = np.asarray(solid) == 128
    if not ring.any():
        raise RuntimeError("could not isolate the logo's ring")
    for _ in range(3):  # include its anti-aliased edge
        grown = ring.copy()
        grown[1:, :] |= ring[:-1, :]
        grown[:-1, :] |= ring[1:, :]
        grown[:, 1:] |= ring[:, :-1]
        grown[:, :-1] |= ring[:, 1:]
        ring = grown

    inner = np.where(ring, 0.0, coverage)
    ys, xs = np.nonzero(inner > 0.05)
    box = (xs.min(), ys.min(), xs.max() + 1, ys.max() + 1)
    mask = Image.fromarray((inner * 255).astype(np.uint8), "L").crop(box)
    side = max(mask.size)
    square = Image.new("L", (side, side), 0)
    square.paste(mask, ((side - mask.size[0]) // 2, (side - mask.size[1]) // 2))
    return square


def hinted(emblem: Image.Image, size: int, colour) -> Image.Image:
    """Small-size frame: black rounded square, crisp ring, large emblem."""
    ss = 8  # supersampling for the shapes
    big = size * ss
    ring = (1 if size <= 24 else 2) * ss
    radius = int(round(size * 0.22)) * ss
    shape = Image.new("L", (big, big), 0)
    draw = ImageDraw.Draw(shape)
    draw.rounded_rectangle((0, 0, big - 1, big - 1), radius=radius, fill=255)
    hole = Image.new("L", (big, big), 0)
    ImageDraw.Draw(hole).rounded_rectangle((ring, ring, big - 1 - ring, big - 1 - ring),
                                            radius=max(radius - ring, 0), fill=255)
    shape_small = shape.resize((size, size), Image.Resampling.BOX)
    hole_small = hole.resize((size, size), Image.Resampling.BOX)

    inset = max(2, round(size * 0.12)) if size > 24 else 2
    inner = size - 2 * inset
    emblem_small = linear_resize_mask(emblem, inner)

    a = np.asarray(shape_small).astype(np.float64) / 255.0
    ring_cov = a - np.asarray(hole_small).astype(np.float64) / 255.0
    emb = np.zeros((size, size))
    emb[inset:inset + inner, inset:inset + inner] = emblem_small
    orange_cov = np.clip(ring_cov + emb * (np.asarray(hole_small) / 255.0), 0.0, 1.0)

    colour_lin = to_linear(np.array(colour, dtype=np.float64) / 255.0)
    rgb_lin = orange_cov[..., None] * colour_lin  # on black
    safe = np.where(a > 1e-6, a, 1.0)
    rgb = to_srgb(np.clip(rgb_lin / safe[..., None], 0.0, 1.0))
    out = np.dstack([rgb, a]) * 255.0
    return Image.fromarray(np.round(out).clip(0, 255).astype(np.uint8), "RGBA")


def linear_resize_mask(mask: Image.Image, size: int) -> np.ndarray:
    data = np.asarray(mask).astype(np.float32) / 255.0
    img = Image.fromarray(data, "F").resize((size, size), Image.Resampling.LANCZOS)
    out = np.clip(np.asarray(img).astype(np.float64), 0.0, 1.0)
    # Coverage mask: push mid-tones slightly so thin strokes keep contrast.
    return np.clip((out - 0.5) * 1.15 + 0.5, 0.0, 1.0)


def paused_variant(master: Image.Image) -> Image.Image:
    """Grey, dimmed version of the logo (monitoring paused or stopped)."""
    data = np.asarray(master).astype(np.float32)
    luma = data[..., 0] * 0.299 + data[..., 1] * 0.587 + data[..., 2] * 0.114
    grey = np.clip(luma * 0.85 + 18.0, 0, 255)
    out = np.dstack([grey, grey, grey, data[..., 3]]).astype(np.uint8)
    return Image.fromarray(out, "RGBA")


def main() -> None:
    source = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "assets" / "logo-master.png"
    master = master_with_alpha(source)
    paused = paused_variant(master)

    assets = ROOT / "assets"
    tray = assets / "tray"
    tray.mkdir(parents=True, exist_ok=True)
    for old in tray.glob("*.png"):
        old.unlink()

    master.save(assets / "logo-source.png")
    resize(master, 512).save(assets / "logo.png")
    resize(master, 256).save(assets / "logo-256.png")

    emblem = emblem_mask(source)
    grey = (170, 170, 170)
    normal_frames, paused_frames = {}, {}
    for size in sorted(set(TRAY_SIZES + ICO_SIZES)):
        if size <= 48:
            normal_frames[size] = hinted(emblem, size, ORANGE)
            paused_frames[size] = hinted(emblem, size, grey)
        else:
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
