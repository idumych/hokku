#!/usr/bin/env python3
# Builds the Gonx and Haiku GTK 3 themes from the sources in src/: each
# theme's gtk.css is the shared rules (common.css) in that look's palette,
# followed by the look's own shapes; the arrow images are drawn here, pixel
# by pixel, the way the two control looks draw them. Run it after editing
# src/ and commit what it writes.

import math
import os
import re
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))

THEMES = {
	"Gonx": ("gonx.css",
		"The Gonx BeOS concept look for GTK 3 applications."),
	"Haiku": ("haiku.css",
		"Haiku's control look for GTK 3 applications."),
}


# PNG writing, without anything outside the standard library

def write_png(path, pixels):
	height = len(pixels)
	width = len(pixels[0])
	raw = b"".join(b"\0" + bytes(c for px in row for c in px)
		for row in pixels)

	def chunk(kind, data):
		body = kind + data
		return (struct.pack(">I", len(data)) + body
			+ struct.pack(">I", zlib.crc32(body) & 0xffffffff))

	with open(path, "wb") as f:
		f.write(b"\x89PNG\r\n\x1a\n")
		f.write(chunk(b"IHDR",
			struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)))
		f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
		f.write(chunk(b"IEND", b""))


def rgb(hex_color):
	h = hex_color.lstrip("#")
	return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def blank(width, height):
	return [[(0, 0, 0, 0)] * width for _ in range(height)]


def gonx_triangle(width, height, direction, outline, fill):
	"""GonxControlLook::_DrawTriangle(): an outlined triangle set pixel by
	pixel, centred in a width x height box."""
	horizontal = direction in ("left", "right")
	across = height if horizontal else width
	along = width if horizontal else height

	length = int(min(across * 0.6, along * 1.6))
	if length % 2 == 0:
		length -= 1
	length = max(length, 5)
	depth = int(max(3, min(round(length * 0.65), along - 2)))

	origin_across = (across - 1) // 2 - length // 2
	origin_along = (along - 1) // 2 - depth // 2

	middle = (length - 1) / 2
	extent = [max(1, int(round(depth * (1 - abs(k - middle) / (middle + 0.5)))))
		for k in range(length)]

	pixels = blank(width, height)
	for k in range(length):
		for j in range(extent[k]):
			edge = (j == 0 or j == extent[k] - 1 or k == 0 or k == length - 1
				or extent[k - 1] <= j or extent[k + 1] <= j)
			a = origin_across + k
			if direction == "right":
				x, y = origin_along + j, a
			elif direction == "left":
				x, y = origin_along + depth - 1 - j, a
			elif direction == "up":
				x, y = a, origin_along + depth - 1 - j
			else:
				x, y = a, origin_along + j
			pixels[y][x] = (outline if edge else fill) + (255,)
	return pixels


def haiku_triangle(width, height, direction, color):
	"""HaikuControlLook::DrawArrowShape(): a small solid arrow, as its thick
	pen leaves it, anti-aliased, centred in a width x height box."""
	horizontal = direction in ("left", "right")
	across = (height if horizontal else width) / 2
	depth = across * 0.6
	cx, cy = width / 2, height / 2
	if direction == "left":
		tri = [(cx + depth / 2, cy - across / 2), (cx - depth / 2, cy),
			(cx + depth / 2, cy + across / 2)]
	elif direction == "right":
		tri = [(cx - depth / 2, cy - across / 2), (cx + depth / 2, cy),
			(cx - depth / 2, cy + across / 2)]
	elif direction == "up":
		tri = [(cx - across / 2, cy + depth / 2), (cx, cy - depth / 2),
			(cx + across / 2, cy + depth / 2)]
	else:
		tri = [(cx - across / 2, cy - depth / 2), (cx, cy + depth / 2),
			(cx + across / 2, cy - depth / 2)]

	def inside(px, py):
		sign = None
		for i in range(3):
			(x1, y1), (x2, y2) = tri[i], tri[(i + 1) % 3]
			d = (x2 - x1) * (py - y1) - (y2 - y1) * (px - x1)
			if d != 0:
				if sign is None:
					sign = d > 0
				elif (d > 0) != sign:
					return False
		return True

	samples = 6
	pixels = blank(width, height)
	for y in range(height):
		for x in range(width):
			hits = sum(inside(x + (i + 0.5) / samples, y + (j + 0.5) / samples)
				for i in range(samples) for j in range(samples))
			if hits:
				alpha = round(255 * hits / samples ** 2)
				pixels[y][x] = color + (alpha,)
	return pixels


def mix(a, b, amount):
	return tuple(round(x + (y - x) * amount) for x, y in zip(a, b))


def make_assets(theme, folder):
	os.makedirs(folder, exist_ok=True)
	directions = ("up", "down", "left", "right")

	def save(name, draw):
		# 1x and 2x, the second drawn at twice the size rather than scaled
		write_png(os.path.join(folder, name + ".png"), draw(1))
		write_png(os.path.join(folder, name + "@2.png"), draw(2))

	if theme == "Gonx":
		track = rgb("#f2f1ee")
		arrow = rgb("#7e7d77")
		white = (255, 255, 255)
		segment_outline = rgb("#95958b")
		segment_fill = rgb("#f0f0e6")
		for d in directions:
			save("scroll-" + d, lambda s, d=d:
				gonx_triangle(13 * s, 13 * s, d, arrow, white))
			save("scroll-" + d + "-disabled", lambda s, d=d:
				gonx_triangle(13 * s, 13 * s, d, mix(arrow, track, 0.55), white))
			save("indicator-" + d, lambda s, d=d:
				gonx_triangle(13 * s, 13 * s, d, segment_outline, segment_fill))
			save("indicator-" + d + "-disabled", lambda s, d=d:
				gonx_triangle(13 * s, 13 * s, d,
					mix(segment_outline, rgb("#ecebe6"), 0.5),
					mix(segment_fill, rgb("#ecebe6"), 0.5)))
	else:
		black = (0, 0, 0)
		grey = rgb("#a8a8a8")
		for d in directions:
			save("scroll-" + d, lambda s, d=d:
				haiku_triangle(13 * s, 13 * s, d, black))
			save("scroll-" + d + "-disabled", lambda s, d=d:
				haiku_triangle(13 * s, 13 * s, d, grey))
			save("indicator-" + d, lambda s, d=d:
				haiku_triangle(13 * s, 13 * s, d, black))
			save("indicator-" + d + "-disabled", lambda s, d=d:
				haiku_triangle(13 * s, 13 * s, d, grey))


# The CSS sources are plain GTK CSS, plus two shorthands for images that
# need the same long value in many places:
#   arrow(name)       an image from assets/, at 1x and 2x
#   cross(color)      the check box mark: two diagonal strokes
#   sliders(:state)   every selector Adwaita gives a scale's slider, so one
#                     rule can replace them all (vsliders(): vertical ones)

SLIDERS = ["scale%s slider%%s" % v for v in (".horizontal",
	".horizontal.fine-tune",
	".horizontal.marks-before:not(.marks-after)",
	".horizontal.marks-after:not(.marks-before)",
	".horizontal.marks-before:not(.marks-after).fine-tune",
	".horizontal.marks-after:not(.marks-before).fine-tune")]


def expand(css):
	# Adwaita draws sliders with marks from images, under long selectors
	css = re.sub(r"\b(v?)sliders\(([\w:-]*)\)",
		lambda m: ", ".join(s.replace(".horizontal", ".vertical")
			if m.group(1) else s for s in SLIDERS) % ((m.group(2),) * 6), css)
	css = re.sub(r"\barrow\(([\w-]+)\)",
		lambda m: '-gtk-scaled(url("assets/%s.png"), url("assets/%s@2.png"))'
			% (m.group(1), m.group(1)), css)

	def cross(m):
		c = m.group(1).strip()
		stroke = ("transparent 40%%, %s 43%%, %s 57%%, transparent 60%%"
			% (c, c))
		return ("linear-gradient(45deg, %s), linear-gradient(-45deg, %s)"
			% (stroke, stroke))

	css = re.sub(r"\bcross\(([^()]+)\)", cross, css)
	return css


HEADER = """/* %(name)s: GTK 3 widgets in Hokku's %(name)s control look.
 * Generated by data/linux/gtk/generate.py from src/common.css and
 * src/%(source)s beside it; edit those and run it again. */

@import url("resource:///org/gtk/libgtk/theme/Adwaita/gtk-contained.css");

"""


def main():
	src = os.path.join(HERE, "src")
	with open(os.path.join(src, "common.css")) as f:
		common = f.read()

	for name, (source, comment) in THEMES.items():
		with open(os.path.join(src, source)) as f:
			look = f.read()
		# the look's palette comes first, then the shared rules, then the
		# look's shapes
		palette, _, shapes = look.partition("/* -- shapes -- */")
		folder = os.path.join(HERE, name, "gtk-3.0")
		os.makedirs(folder, exist_ok=True)
		with open(os.path.join(folder, "gtk.css"), "w") as f:
			f.write(HEADER % {"name": name, "source": source})
			f.write(expand(palette.strip()) + "\n\n")
			f.write(expand(common.strip()) + "\n\n")
			f.write(expand(shapes.strip()) + "\n")
		make_assets(name, os.path.join(folder, "assets"))

		with open(os.path.join(HERE, name, "index.theme"), "w") as f:
			f.write("[Desktop Entry]\n"
				"Type=X-GNOME-Metatheme\n"
				"Name=%s\n"
				"Comment=%s\n"
				"Encoding=UTF-8\n"
				"\n"
				"[X-GNOME-Metatheme]\n"
				"GtkTheme=%s\n" % (name, comment, name))


if __name__ == "__main__":
	main()
