import json
import os
import sys

from PIL import Image, ImageDraw, ImageFont


def fit(image, size):
    image = image.copy()
    image.thumbnail(size, Image.Resampling.LANCZOS)
    canvas = Image.new("RGB", size, "#090b0e")
    canvas.paste(image, ((size[0] - image.width) // 2, (size[1] - image.height) // 2))
    return canvas


def font(candidates, size):
    for path in candidates:
        if os.path.exists(path):
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


def main():
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} CAPTURE_DIRECTORY", file=sys.stderr)
        return 2

    folder = sys.argv[1]
    with open(os.path.join(folder, "metadata.json"), encoding="utf-8") as stream:
        metadata = json.load(stream)

    regular_fonts = [
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    ]
    bold_fonts = [
        "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    ]
    title_font = font(bold_fonts, 35)
    label_font = font(bold_fonts, 25)
    detail_font = font(regular_fonts, 20)

    width, panel_height = 800, 450
    top, bottom = 70, 60
    sheet = Image.new("RGB", (width * 2, top + panel_height * 2 + bottom), "#11151a")
    draw = ImageDraw.Draw(sheet)
    draw.text(
        (30, 17), "Kinect v2 synchronized capture", font=title_font, fill="#f4f7fa"
    )

    panels = [
        ("rgb.ppm", "RGB • 1920×1080"),
        ("infrared.pgm", "Infrared • 512×424"),
        ("depth_false_color.ppm", "Depth • near red → far blue"),
        ("registered.ppm", "RGB registered to depth • 512×424"),
    ]
    for index, (filename, label) in enumerate(panels):
        image = Image.open(os.path.join(folder, filename)).convert("RGB")
        panel = fit(image, (width, panel_height))
        x = (index % 2) * width
        y = top + (index // 2) * panel_height
        sheet.paste(panel, (x, y))
        overlay = Image.new("RGBA", (width, 47), (0, 0, 0, 165))
        sheet.paste(overlay, (x, y), overlay)
        draw.text((x + 18, y + 9), label, font=label_font, fill="white")

    summary = (
        f"{metadata['pipeline']} pipeline  •  firmware {metadata['firmware']}  •  "
        f"{metadata['valid_depth_pixels']:,} valid depth pixels ({metadata['valid_depth_percent']:.2f}%)  •  "
        f"range {metadata['minimum_depth_mm']:.0f}–{metadata['maximum_depth_mm']:.0f} mm"
    )
    draw.text(
        (30, top + panel_height * 2 + 18), summary, font=detail_font, fill="#cbd5df"
    )
    sheet.save(os.path.join(folder, "kinect_contact_sheet.png"), optimize=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
