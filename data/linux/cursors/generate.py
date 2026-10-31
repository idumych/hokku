#!/usr/bin/env python3
# Builds the Xcursor themes Hokku and Hokku-Shadow: BeOS's pointing hand as
# the pointer and its I-beam over text, taken from B_HAND_CURSOR and
# B_I_BEAM_CURSOR (src/kits/app/AppDefs.cpp), so that the desktop and Linux
# apps show the cursor Be apps have always had. Hokku-Shadow gives them the
# shadow of the Haiku and Gonx window frames (gonx_frame_paint_shadow()),
# scaled to the cursor: a light above and to the left. Every other cursor
# (resizing, busy, ...) comes from Adwaita.
#
# The 16x16 bitmaps are scaled by whole numbers (16, 32, 48 and 64 pixels),
# so that they stay crisp; the shadow is drawn at each size. They answer to
# the cursor sizes 24, 48, 72 and 96: 24, what labwc and GTK ask for by
# default, gets BeOS's own 16 pixels, and a screen at 2x the 32 pixel one.
#
# Run it after a change and commit what it writes:
#
#	python3 data/linux/cursors/generate.py
#
# It rewrites Hokku/ and Hokku-Shadow/ next to itself.

import math
import os
import re
import shutil
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
APPDEFS = os.path.join(HERE, "..", "..", "..", "src", "kits", "app",
	"AppDefs.cpp")

SCALES = (1, 2, 3, 4)
NOMINAL = 24	# the cursor size a 1x cursor answers to

# The cursor's shadow at 1x: offset right and down, gaussian sigma, opacity.
# The window frames' is (6, 9, 3, 0.6) for a window; this is it for 16
# pixels.
SHADOW = (1, 2, 1.0, 0.5)

# Xcursor names: the pointer and the text cursor, under the names X11 and the
# cursor spec give them
CURSORS = {
	"B_HAND_CURSOR": ("default", "left_ptr", "arrow", "top_left_arrow",
		"left_arrow"),
	"B_I_BEAM_CURSOR": ("text", "xterm", "ibeam"),
}


def be_cursor(name):
	"""The size, hot spot and pixels (None, 'black' or 'white') of a Be
	cursor in AppDefs.cpp: a 1-bit image over a 1-bit transparency mask."""
	source = open(APPDEFS).read()
	body = re.search(r"%s\[\]\s*=\s*\{(.*?)\};" % name, source, re.S).group(1)
	body = re.sub(r"//[^\n]*", "", body)
	values = [int(v, 0) for v in re.findall(r"0x[0-9a-fA-F]+|\d+", body)]
	size, depth, hot_x, hot_y = values[:4]
	assert size == 16 and depth == 1
	image, mask = values[4:36], values[36:68]

	pixels = []
	for y in range(16):
		row = []
		for x in range(16):
			byte = y * 2 + x // 8
			bit = 0x80 >> (x % 8)
			if not mask[byte] & bit:
				row.append(None)
			else:
				row.append("black" if image[byte] & bit else "white")
		pixels.append(row)
	return hot_x, hot_y, pixels


def blur(alpha, width, height, sigma):
	radius = int(math.ceil(3 * sigma))
	kernel = [math.exp(-i * i / (2 * sigma * sigma))
		for i in range(-radius, radius + 1)]
	total = sum(kernel)
	kernel = [k / total for k in kernel]

	def line(values):
		out = []
		for i in range(len(values)):
			v = 0.0
			for k, weight in enumerate(kernel):
				j = i + k - radius
				if 0 <= j < len(values):
					v += weight * values[j]
			out.append(v)
		return out

	rows = [line(alpha[y * width:(y + 1) * width]) for y in range(height)]
	flat = [v for row in rows for v in row]
	columns = [line(flat[x::width]) for x in range(width)]
	return [columns[x][y] for y in range(height) for x in range(width)]


def render(cursor, scale, shadow):
	"""ARGB pixels (premultiplied), width, height and hot spot"""
	hot_x, hot_y, pixels = cursor
	size = 16 * scale
	if shadow:
		dx, dy, sigma, opacity = SHADOW
		dx, dy, sigma = dx * scale, dy * scale, sigma * scale
		reach = int(math.ceil(3 * sigma))
		width, height = size + dx + reach, size + dy + reach
	else:
		width, height = size, size

	argb = [(0, 0, 0, 0)] * (width * height)
	if shadow:
		shape = [0.0] * (width * height)
		for y in range(size):
			for x in range(size):
				if pixels[y // scale][x // scale] is not None:
					shape[(y + dy) * width + x + dx] = 1.0
		for i, a in enumerate(blur(shape, width, height, sigma)):
			argb[i] = (a * opacity, 0, 0, 0)

	for y in range(size):
		for x in range(size):
			p = pixels[y // scale][x // scale]
			if p is not None:
				v = 0 if p == "black" else 1
				argb[y * width + x] = (1.0, v, v, v)

	data = bytearray()
	for a, r, g, b in argb:
		data += struct.pack("<I", (round(a * 255) << 24)
			| (round(r * a * 255) << 16) | (round(g * a * 255) << 8)
			| round(b * a * 255))
	return bytes(data), width, height, hot_x * scale, hot_y * scale


def xcursor(images):
	"""An Xcursor file of (nominal size, pixels, width, height, hot x,
	hot y) images"""
	header_size = 16
	toc = []
	chunks = b""
	offset = header_size + 12 * len(images)
	for nominal, data, width, height, hot_x, hot_y in images:
		chunk = struct.pack("<9I", 36, 0xfffd0002, nominal, 1, width, height,
			hot_x, hot_y, 0) + data
		toc.append(struct.pack("<3I", 0xfffd0002, nominal, offset))
		offset += len(chunk)
		chunks += chunk
	return (struct.pack("<4I", 0x72756358, header_size, 0x10000, len(images))
		+ b"".join(toc) + chunks)


def write_theme(name, shadow):
	folder = os.path.join(HERE, name)
	shutil.rmtree(folder, ignore_errors=True)
	os.makedirs(os.path.join(folder, "cursors"))
	with open(os.path.join(folder, "index.theme"), "w") as f:
		f.write("[Icon Theme]\nName=%s\nComment=BeOS's pointing hand and "
			"I-beam%s; the rest from Adwaita\nInherits=Adwaita\n"
			% (name, ", with a shadow" if shadow else ""))

	for symbol, names in CURSORS.items():
		cursor = be_cursor(symbol)
		images = [(NOMINAL * scale,) + render(cursor, scale, shadow)
			for scale in SCALES]
		data = xcursor(images)
		for cursor_name in names:
			with open(os.path.join(folder, "cursors", cursor_name), "wb") as f:
				f.write(data)


write_theme("Hokku", shadow=False)
write_theme("Hokku-Shadow", shadow=True)
