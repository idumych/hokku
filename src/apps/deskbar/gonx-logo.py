#!/usr/bin/env python3
"""gonx-logo.py: writes the Deskbar's Gonx menu button logo as an HVIF
vector icon resource (R_GonxLogoBitmap in GonxLogo.rdef).

The logo is a small Gonx window tab: yellow, outlined, with the hatched flap
turning down at its left end, and "Hokku" on it. The Deskbar draws the icon
at 100x100 (more with larger fonts), centred in the menu button and clipped
to it, so everything sits in rows 41 to 58, which a 20 pixel high button
shows. Colours are sampled from the Gonx decorator's tab.

Needs pycairo and Noto Sans Bold (for the lettering, turned into outlines):

    python3 gonx-logo.py GonxLogo.rdef [preview.svg]
"""
import math
import struct
import sys

import cairo

FONT = "Noto Sans"

# Geometry, in pixels of the 100x100 icon.
LEFT, TOP, RIGHT, BOTTOM = 6, 41, 95, 54	# the tab
FLAP_RIGHT, FLAP_BOTTOM = 15, 58			# the flap below its left end
TOP_RADIUS, END_RADIUS, FOOT_RADIUS = 6, 7, 2

OUTLINE = (70, 57, 0, 255)
HIGHLIGHT = (253, 235, 153, 255)
YELLOW_TOP = (250, 219, 84, 255)
YELLOW_BOTTOM = (221, 185, 44, 255)
HATCH = (246, 230, 150, 255)
TEXT = (36, 56, 90, 255)

SCALE = 64 / 100	# HVIF coordinates run from 0 to 64
KAPPA = 0.5523		# control point distance for a quarter circle


# #pragma mark - paths

class Path:
	"""A closed path of HVIF points: (point, in control, out control)."""
	def __init__(self):
		self.points = []

	def move(self, x, y):
		self.points.append([(x, y), (x, y), (x, y)])

	def line(self, x, y):
		self.move(x, y)

	def curve(self, c1, c2, end):
		self.points[-1][2] = c1
		self.points.append([end, c2, end])

	def corner(self, cx, cy, radius, start, end):
		"""A quarter circle round (cx, cy) from angle start to end, in
		degrees, y down; the path must be at its start already."""
		a0, a1 = math.radians(start), math.radians(end)
		p0 = (cx + radius * math.cos(a0), cy + radius * math.sin(a0))
		p1 = (cx + radius * math.cos(a1), cy + radius * math.sin(a1))
		k = KAPPA * radius * (1 if a1 > a0 else -1)
		c1 = (p0[0] - k * math.sin(a0), p0[1] + k * math.cos(a0))
		c2 = (p1[0] + k * math.sin(a1), p1[1] - k * math.cos(a1))
		self.curve(c1, c2, p1)

	def close(self):
		# a closed path's last point coincides with its first: fold the
		# last point's incoming control into the first point
		first, last = self.points[0], self.points[-1]
		if (abs(first[0][0] - last[0][0]) < 1e-6
				and abs(first[0][1] - last[0][1]) < 1e-6):
			first[1] = last[1]
			self.points.pop()


def tab_outline(inset):
	"""The tab and its flap as one shape, inset by the given amount."""
	left, top, right = LEFT + inset, TOP + inset, RIGHT - inset
	bottom, flap_right = BOTTOM - inset, FLAP_RIGHT - inset
	flap_bottom = FLAP_BOTTOM - inset
	top_r, end_r = TOP_RADIUS - inset, END_RADIUS - inset
	foot_r = max(FOOT_RADIUS - inset, 0.5)
	flap_r = flap_right - left	# a quarter circle as wide as the flap

	p = Path()
	p.move(left + top_r, top)
	p.line(right - end_r, top)
	p.corner(right - end_r, top + end_r, end_r, -90, 0)
	p.line(right, bottom - foot_r)
	p.corner(right - foot_r, bottom - foot_r, foot_r, 0, 90)
	p.line(flap_right, bottom)
	p.line(flap_right, flap_bottom)
	p.corner(flap_right, flap_bottom - flap_r, flap_r, 90, 180)
	p.line(left, top + top_r)
	p.corner(left + top_r, top + top_r, top_r, 180, 270)
	p.close()
	return p


def polygon(points):
	p = Path()
	for x, y in points:
		p.line(x, y)
	return p


def clip(subject, keep):
	"""Clips a polygon to the half plane where keep(x, y) >= 0."""
	out = []
	for i, a in enumerate(subject):
		b = subject[(i + 1) % len(subject)]
		fa, fb = keep(*a), keep(*b)
		if fa >= 0:
			out.append(a)
		if (fa >= 0) != (fb >= 0):
			t = fa / (fa - fb)
			out.append((a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1])))
	return out


def hatch():
	"""Pale diagonal stripes down the flap, as on the Gonx tab."""
	left, right = LEFT + 1.5, FLAP_RIGHT - 1
	top, bottom = TOP + 3, FLAP_BOTTOM - 1
	flap_r = FLAP_RIGHT - 1 - (LEFT + 1)
	cx, cy = FLAP_RIGHT - 1, FLAP_BOTTOM - 1 - flap_r
	stripes = []
	for offset in range(-12, 22, 3):
		x0 = left + offset
		band = [(x0, bottom), (x0 + 1.2, bottom), (x0 + 1.2 + 24, bottom - 24),
			(x0 + 24, bottom - 24)]
		band = clip(band, lambda x, y: x - left)
		band = clip(band, lambda x, y: right - x)
		band = clip(band, lambda x, y: y - top)
		band = clip(band, lambda x, y: bottom - y)
		# stay inside the flap's rounded foot: tangent half planes
		for angle in range(95, 180, 12):
			a = math.radians(angle)
			nx, ny = math.cos(a), math.sin(a)
			band = clip(band, lambda x, y, nx=nx, ny=ny:
				(flap_r - 0.8) - ((x - cx) * nx + (y - cy) * ny))
		if len(band) >= 3:
			stripes.append(polygon(band))
	return stripes


def lettering(text, size, centre_x, baseline):
	"""The text's glyph outlines, as one path per contour."""
	surface = cairo.RecordingSurface(cairo.CONTENT_ALPHA, None)
	cr = cairo.Context(surface)
	cr.select_font_face(FONT, cairo.FONT_SLANT_NORMAL, cairo.FONT_WEIGHT_BOLD)
	cr.set_font_size(size)
	extents = cr.text_extents(text)
	cr.move_to(centre_x - extents.x_advance / 2, baseline)
	cr.text_path(text)

	paths = []
	current = None
	for kind, points in cr.copy_path():
		if kind == cairo.PATH_MOVE_TO:
			current = Path()
			current.move(*points)
		elif kind == cairo.PATH_LINE_TO:
			current.line(*points)
		elif kind == cairo.PATH_CURVE_TO:
			current.curve(points[0:2], points[2:4], points[4:6])
		elif kind == cairo.PATH_CLOSE_PATH:
			current.close()
			paths.append(current)
			current = None
	return paths


# #pragma mark - HVIF

STYLE_SOLID = 1
STYLE_GRADIENT = 2
GRADIENT_LINEAR = 0
GRADIENT_FLAG_TRANSFORM = 1 << 1
PATH_FLAG_CLOSED = 1 << 1
SHAPE_PATH_SOURCE = 10


def coord(value):
	value *= SCALE
	fixed = int(round((value + 128.0) * 102.0))
	return bytes([0x80 | (fixed >> 8), fixed & 0xff])


def float24(value):
	if value == 0:
		return b"\0\0\0"
	bits = struct.unpack("<I", struct.pack("<f", value))[0]
	sign = bits >> 31
	exponent = ((bits >> 23) & 0xff) - 127 + 32
	mantissa = (bits & 0x7fffff) >> 6
	packed = (sign << 23) | (exponent << 17) | mantissa
	return bytes([packed >> 16, (packed >> 8) & 0xff, packed & 0xff])


def solid(color):
	return {"kind": "solid", "color": color}


def gradient(top, bottom, y0, y1):
	return {"kind": "gradient", "top": top, "bottom": bottom,
		"y0": y0, "y1": y1}


def encode_style(style):
	if style["kind"] == "solid":
		return bytes([STYLE_SOLID]) + bytes(style["color"])
	# gradient space runs from -64 to 64 along x: map it onto y0..y1
	y0, y1 = style["y0"] * SCALE, style["y1"] * SCALE
	half = (y1 - y0) / 128.0
	matrix = [0.0, half, -half, 0.0, 32.0, (y0 + y1) / 2]
	out = bytes([STYLE_GRADIENT, GRADIENT_LINEAR, GRADIENT_FLAG_TRANSFORM, 2])
	for value in matrix:
		out += float24(value)
	out += bytes([0]) + bytes(style["top"])
	out += bytes([255]) + bytes(style["bottom"])
	return out


def encode_path(path):
	assert 2 <= len(path.points) <= 255
	out = bytes([PATH_FLAG_CLOSED, len(path.points)])
	for point, control_in, control_out in path.points:
		for x, y in (point, control_in, control_out):
			out += coord(x) + coord(y)
	return out


def build():
	styles = [
		solid(OUTLINE),
		gradient(YELLOW_TOP, YELLOW_BOTTOM, TOP + 1, FLAP_BOTTOM),
		solid(HIGHLIGHT),
		solid(HATCH),
		solid(TEXT),
	]
	shapes = []	# (style, [paths])
	shapes.append((0, [tab_outline(0)]))
	shapes.append((1, [tab_outline(1)]))
	shapes.append((2, [polygon([(LEFT + TOP_RADIUS, TOP + 1),
		(RIGHT - END_RADIUS, TOP + 1), (RIGHT - END_RADIUS + 1.5, TOP + 2),
		(LEFT + TOP_RADIUS - 1.5, TOP + 2)])]))
	shapes.append((3, hatch()))
	shapes.append((4, lettering("Hokku", 12.2,
		(FLAP_RIGHT + RIGHT) / 2 + 0.5, 51.6)))
	return styles, shapes


def encode(styles, shapes):
	paths = [path for _, shape_paths in shapes for path in shape_paths]
	assert len(paths) <= 255
	out = b"ncif" + bytes([len(styles)])
	for style in styles:
		out += encode_style(style)
	out += bytes([len(paths)])
	for path in paths:
		out += encode_path(path)
	out += bytes([len(shapes)])
	index = 0
	for style, shape_paths in shapes:
		out += bytes([SHAPE_PATH_SOURCE, style, len(shape_paths)])
		out += bytes(range(index, index + len(shape_paths)))
		out += bytes([0])
		index += len(shape_paths)
	return out


def svg(styles, shapes):
	lines = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" '
		'width="400" height="400">', '<defs>']
	for index, style in enumerate(styles):
		if style["kind"] == "gradient":
			lines.append(
				f'<linearGradient id="g{index}" gradientUnits="userSpaceOnUse"'
				f' x1="0" y1="{style["y0"]}" x2="0" y2="{style["y1"]}">'
				f'<stop offset="0" stop-color="rgb{style["top"][:3]}"/>'
				f'<stop offset="1" stop-color="rgb{style["bottom"][:3]}"/>'
				'</linearGradient>')
	lines.append('</defs>')
	for style_index, shape_paths in shapes:
		style = styles[style_index]
		fill = (f'url(#g{style_index})' if style["kind"] == "gradient"
			else f'rgb{style["color"][:3]}')
		data = []
		for path in shape_paths:
			points = path.points
			data.append(f'M{points[0][0][0]:.2f},{points[0][0][1]:.2f}')
			for i in range(len(points)):
				a, b = points[i], points[(i + 1) % len(points)]
				data.append(f'C{a[2][0]:.2f},{a[2][1]:.2f} '
					f'{b[1][0]:.2f},{b[1][1]:.2f} {b[0][0]:.2f},{b[0][1]:.2f}')
			data.append('Z')
		lines.append(f'<path d="{" ".join(data)}" fill="{fill}"/>')
	lines.append('</svg>')
	return "\n".join(lines)


def main():
	styles, shapes = build()
	data = encode(styles, shapes)
	hex_data = data.hex().upper()
	rows = [hex_data[i:i + 64] for i in range(0, len(hex_data), 64)]
	with open(sys.argv[1], "w") as rdef:
		rdef.write("// Generated by gonx-logo.py; don't edit.\n\n")
		rdef.write('#include "icons.h"\n\n')
		rdef.write("resource(R_GonxLogoBitmap) vector_icon {\n")
		for row in rows:
			rdef.write(f'\t$"{row}"\n')
		rdef.write("};\n")
	if len(sys.argv) > 2:
		with open(sys.argv[2], "w") as preview:
			preview.write(svg(styles, shapes))
	print(f"{len(data)} bytes of HVIF")


if __name__ == "__main__":
	main()
