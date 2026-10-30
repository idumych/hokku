#!/usr/bin/env python3
# Builds the Kvantum themes Gonx and Haiku, so that Qt 5/6 applications
# (QtStyle=kvantum) draw their widgets like Hokku's two control looks:
# GonxControlLook and Haiku's own HaikuControlLook.
#
# Each widget part is drawn here once, in pixels, with the colour rules of
# the control look it copies; a theme is a palette plus a few sizes. Kvantum
# cuts most parts into nine pieces (four corners, four edges, the interior)
# and stretches the edges and the interior, so every part is drawn at a small
# size whose middle row and column can be stretched, and then sliced.
#
# Run it after a change and commit what it writes:
#
#	python3 data/linux/kvantum/generate.py
#
# It rewrites Gonx/Gonx.svg, Gonx/Gonx.kvconfig, Haiku/Haiku.svg and
# Haiku/Haiku.kvconfig next to itself.

import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))

STATES = ("normal", "focused", "pressed", "toggled", "disabled")
WHITE = (255, 255, 255)
BLACK = (0, 0, 0)


# colours

def rgb(hex_color):
	h = hex_color.lstrip("#")
	return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def hex_color(color):
	return "#%02x%02x%02x" % color


def tint(color, amount):
	"""Haiku's tint_color(): below 1 lightens, above 1 darkens."""
	if amount < 1:
		return tuple(int(255 - (255 - c) * amount) for c in color)
	return tuple(int(c * (2 - amount)) for c in color)


def mix(a, b, amount):
	"""Gonx's mix(): from a towards b."""
	return tuple(int(math.floor(x + (y - x) * amount + 0.5))
		for x, y in zip(a, b))


class Gradient:
	"""A linear gradient in a part's own pixel coordinates."""

	def __init__(self, x1, y1, x2, y2, stops):
		self.x1, self.y1, self.x2, self.y2 = x1, y1, x2, y2
		# (offset, colour) or (offset, colour, opacity)
		self.stops = [s if len(s) == 3 else (s[0], s[1], 1.0) for s in stops]

	def faded(self, towards, amount):
		return Gradient(self.x1, self.y1, self.x2, self.y2,
			[(o, mix(c, towards, amount), a) for o, c, a in self.stops])


def vertical(x, y, h, stops):
	# Haiku's gradients run from the centre of the first row to the
	# centre of the last
	return Gradient(x, y + 0.5, x, y + h - 0.5, stops)


def horizontal(x, y, w, stops):
	return Gradient(x + 0.5, y, x + w - 0.5, y, stops)


# drawing: a part is a list of shapes in its own pixel coordinates, later
# ones covering earlier ones, much like the control looks paint

class Painter:
	def __init__(self, width, height):
		self.width = width
		self.height = height
		self.items = []

	def fill(self, x, y, w, h, paint, opacity=1.0):
		if w > 0 and h > 0 and paint is not None:
			self.items.append(("rect", (x, y, w, h), paint, opacity, None))

	def path(self, commands, box, paint, opacity=1.0, stroke=None):
		"""A path, as ("M", x, y), ("L", x, y), ("A", r, sweep, x, y) and
		("Z",) commands. \a box is what it covers; it must stay within one
		slice of the part. \a stroke is (width, colour) to outline it
		instead of filling it."""
		self.items.append(("path", commands, paint, opacity, (box, stroke)))

	def ellipse(self, x, y, w, h, paint, opacity=1.0):
		self.path(ellipse_path(x, y, w, h), (x, y, w, h), paint, opacity)

	def frame(self, rect, left, top, right, bottom):
		"""Haiku's _DrawFrame(): one-pixel lines, left first, then top,
		right and bottom, each inside the ones before. Returns what is
		left inside."""
		x, y, w, h = rect
		self.fill(x, y, 1, h, left)
		x += 1
		w -= 1
		self.fill(x, y, w, 1, top)
		y += 1
		h -= 1
		self.fill(x + w - 1, y, 1, h, right)
		w -= 1
		self.fill(x, y + h - 1, w, 1, bottom)
		h -= 1
		return (x, y, w, h)

	def spans(self, rows, paint, opacity=1.0):
		"""Fills (y, x0, x1) row spans, x1 excluded. Equal spans on
		consecutive rows become one rectangle, so that stretching them
		leaves no seams."""
		runs = []
		for y, x0, x1 in sorted(rows):
			for run in runs:
				if run[1] == x0 and run[2] == x1 and run[0] + run[3] == y:
					run[3] += 1
					break
			else:
				runs.append([y, x0, x1, 1])
		for y, x0, x1, h in runs:
			self.fill(x0, y, x1 - x0, h, paint, opacity)

	def faded(self, towards, amount):
		"""The same part, its colours mixed towards \a towards: how both
		looks draw disabled controls."""
		copy = Painter(self.width, self.height)
		for kind, data, paint, opacity, extra in self.items:
			if isinstance(paint, Gradient):
				paint = paint.faded(towards, amount)
			elif paint is not None:
				paint = mix(paint, towards, amount)
			if extra is not None and extra[1] is not None:
				width, color = extra[1]
				extra = (extra[0], (width, mix(color, towards, amount)))
			copy.items.append((kind, data, paint, opacity, extra))
		return copy


def ellipse_path(x, y, w, h):
	rx, ry = w / 2, h / 2
	cx, cy = x + rx, y + ry
	return [("M", cx - rx, cy), ("E", rx, ry, cx + rx, cy),
		("E", rx, ry, cx - rx, cy), ("Z",)]


def corner_inset(radius, row):
	"""GonxControlLook's corner_inset(): how far row \a row of a rounded
	corner is pushed in."""
	if row >= radius:
		return 0
	distance = radius - row - 0.5
	return math.floor(radius
		- math.sqrt(max(radius * radius - distance * distance, 0)) + 0.5)


def rounded_rows(rect, radius):
	"""The rows of Gonx's fill_rounded(): a rounded rectangle with crisp
	pixel steps for corners, as (y, x0, x1) spans."""
	x, y, w, h = rect
	if w <= 0 or h <= 0:
		return []
	rows = []
	steps = int(min(radius, (h + 1) // 2))
	for row in range(h):
		k = min(row, h - 1 - row)
		inset = corner_inset(radius, k) if k < steps else 0
		rows.append((y + row, x + inset, x + w - inset))
	return rows


def subtract_rows(outer, inner):
	"""The parts of \a outer's spans that \a inner's leave uncovered."""
	cut = {y: (x0, x1) for y, x0, x1 in inner}
	rows = []
	for y, x0, x1 in outer:
		if y not in cut:
			rows.append((y, x0, x1))
			continue
		c0, c1 = cut[y]
		if c0 > x0:
			rows.append((y, x0, min(c0, x1)))
		if c1 < x1:
			rows.append((y, max(c1, x0), x1))
	return rows


def gonx_triangle(rect, direction):
	"""GonxControlLook::_DrawTriangle(): an outlined triangle set pixel by
	pixel, centred in \a rect. Returns (outline, inside) pixel lists."""
	x0, y0, w, h = rect
	horizontal_arrow = direction in ("left", "right")
	across = h if horizontal_arrow else w
	along = w if horizontal_arrow else h

	length = int(min(across * 0.6, along * 1.6))
	if length % 2 == 0:
		length -= 1
	length = max(length, 5)
	depth = int(max(3, min(round(length * 0.65), along - 2)))

	origin_across = ((y0 + y0 + h - 1) if horizontal_arrow
		else (x0 + x0 + w - 1)) // 2 - length // 2
	origin_along = ((x0 + x0 + w - 1) if horizontal_arrow
		else (y0 + y0 + h - 1)) // 2 - depth // 2

	middle = (length - 1) / 2
	extent = [max(1, int(round(depth * (1 - abs(k - middle) / (middle + 0.5)))))
		for k in range(length)]

	outline, inside = [], []
	for k in range(length):
		for j in range(extent[k]):
			edge = (j == 0 or j == extent[k] - 1 or k == 0 or k == length - 1
				or extent[k - 1] <= j or extent[k + 1] <= j)
			a = origin_across + k
			if direction == "right":
				px, py = origin_along + j, a
			elif direction == "left":
				px, py = origin_along + depth - 1 - j, a
			elif direction == "up":
				px, py = a, origin_along + depth - 1 - j
			else:
				px, py = a, origin_along + j
			(outline if edge else inside).append((px, py))
	return outline, inside


# the SVG sheet: every part is a group with an invisible rectangle that
# gives it its exact size, wherever its shapes reach

def number(value):
	text = "%.3f" % value
	return text.rstrip("0").rstrip(".") if "." in text else text


class Sheet:
	def __init__(self):
		self.parts = []
		self.gradients = {}
		self.ids = set()
		self.x = self.y = 0
		self.row_height = 0
		self.width = 0

	def _place(self, w, h):
		if self.x + w > 1200:
			self.x = 0
			self.y += self.row_height + 6
			self.row_height = 0
		x, y = self.x, self.y
		self.x += w + 6
		self.row_height = max(self.row_height, h)
		self.width = max(self.width, self.x)
		return x, y

	def _paint(self, paint, dx, dy):
		if not isinstance(paint, Gradient):
			return hex_color(paint)
		key = (id(paint), dx, dy)
		if key not in self.gradients:
			name = "g%d" % len(self.gradients)
			stops = "".join(
				'<stop offset="%s" style="stop-color:%s;stop-opacity:%s"/>'
				% (number(o), hex_color(c), number(a))
				for o, c, a in paint.stops)
			self.gradients[key] = (name,
				'<linearGradient id="%s" gradientUnits="userSpaceOnUse" '
				'x1="%s" y1="%s" x2="%s" y2="%s">%s</linearGradient>'
				% (name, number(paint.x1 + dx), number(paint.y1 + dy),
					number(paint.x2 + dx), number(paint.y2 + dy), stops),
				paint)
		return "url(#%s)" % self.gradients[key][0]

	def add(self, name, painter, region=None):
		"""Adds \a region (all of it by default) of \a painter's part as
		the element \a name."""
		if name in self.ids:
			raise ValueError("element %s is drawn twice" % name)
		self.ids.add(name)
		if region is None:
			region = (0, 0, painter.width, painter.height)
		rx, ry, rw, rh = region
		px, py = self._place(rw, rh)
		dx, dy = px - rx, py - ry

		out = ['<g id="%s">' % name,
			'<rect x="%d" y="%d" width="%d" height="%d" '
			'style="fill:#000000;fill-opacity:0"/>' % (px, py, rw, rh)]
		for kind, data, paint, opacity, extra in painter.items:
			alpha = "" if opacity >= 1 else ";fill-opacity:%s" % number(opacity)
			if kind == "rect":
				x, y, w, h = data
				x0, y0 = max(x, rx), max(y, ry)
				x1, y1 = min(x + w, rx + rw), min(y + h, ry + rh)
				if x1 <= x0 or y1 <= y0:
					continue
				out.append('<rect x="%s" y="%s" width="%s" height="%s" '
					'style="fill:%s%s"/>' % (number(x0 + dx), number(y0 + dy),
						number(x1 - x0), number(y1 - y0),
						self._paint(paint, dx, dy), alpha))
				continue

			(bx, by, bw, bh), stroke = extra
			if bx >= rx + rw or by >= ry + rh or bx + bw <= rx or by + bh <= ry:
				continue
			if bx < rx or by < ry or bx + bw > rx + rw or by + bh > ry + rh:
				raise ValueError("a shape of %s crosses its slice" % name)
			d = []
			for c in data:
				if c[0] in ("M", "L"):
					d.append("%s%s,%s" % (c[0], number(c[1] + dx),
						number(c[2] + dy)))
				elif c[0] == "A":
					d.append("A%s,%s 0 0,%d %s,%s" % (number(c[1]),
						number(c[1]), c[2], number(c[3] + dx), number(c[4] + dy)))
				elif c[0] == "E":
					d.append("A%s,%s 0 0,1 %s,%s" % (number(c[1]),
						number(c[2]), number(c[3] + dx), number(c[4] + dy)))
				else:
					d.append("Z")
			if stroke is None:
				style = "fill:%s%s" % (self._paint(paint, dx, dy), alpha)
			else:
				width, color = stroke
				style = ("fill:none;stroke:%s;stroke-width:%s;"
					"stroke-linecap:%s;stroke-linejoin:miter%s"
					% (hex_color(color), number(width), paint,
						"" if opacity >= 1 else ";stroke-opacity:%s"
							% number(opacity)))
			out.append('<path d="%s" style="%s"/>' % (" ".join(d), style))
		out.append("</g>")
		self.parts.append("".join(out))

	def nine(self, frame, interior, state, painter, widths):
		"""Adds a part cut the way Kvantum draws frames: the interior
		\a interior-state and the frame pieces frame-state-top and so on.
		Either name may be None."""
		left, top, right, bottom = widths
		w, h = painter.width, painter.height
		columns = ((0, left), (left, w - left - right), (w - right, right))
		rows = ((0, top), (top, h - top - bottom), (h - bottom, bottom))
		names = (("topleft", "top", "topright"), ("left", None, "right"),
			("bottomleft", "bottom", "bottomright"))
		for i, (y, rh) in enumerate(rows):
			for j, (x, cw) in enumerate(columns):
				piece = names[i][j]
				if piece is None:
					if interior is not None:
						self.add("%s-%s" % (interior, state), painter,
							(x, y, cw, rh))
				elif frame is not None and cw > 0 and rh > 0:
					self.add("%s-%s-%s" % (frame, state, piece), painter,
						(x, y, cw, rh))

	def svg(self, title):
		height = self.y + self.row_height
		defs = "".join(g[1] for g in self.gradients.values())
		return ('<?xml version="1.0" encoding="UTF-8"?>\n'
			'<!-- %s: written by generate.py, edit that instead -->\n'
			'<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" '
			'viewBox="0 0 %d %d">\n<defs>%s</defs>\n%s\n</svg>\n'
			% (title, self.width, height, self.width, height, defs,
				"\n".join(self.parts)))


def blank(w=4, h=4):
	return Painter(w, h)


def lines(painter, color, points, width):
	"""An open polyline with butt ends, as Haiku strokes arrows."""
	xs = [p[0] for p in points]
	ys = [p[1] for p in points]
	pad = width
	box = (math.floor(min(xs) - pad), math.floor(min(ys) - pad),
		math.ceil(max(xs) - min(xs) + 2 * pad) + 1,
		math.ceil(max(ys) - min(ys) + 2 * pad) + 1)
	box = (max(box[0], 0), max(box[1], 0),
		min(box[2], painter.width - max(box[0], 0)),
		min(box[3], painter.height - max(box[1], 0)))
	commands = [("M",) + points[0]] + [("L",) + p for p in points[1:]]
	painter.path(commands, box, "butt", stroke=(width, color))


def round_line(painter, color, a, b, width):
	box = (0, 0, painter.width, painter.height)
	painter.path([("M",) + a, ("L",) + b], box, "round", stroke=(width, color))


def pixels(painter, points, color):
	for x, y in points:
		painter.fill(x, y, 1, 1, color)


def triangle(painter, points, color):
	xs = [p[0] for p in points]
	ys = [p[1] for p in points]
	box = (math.floor(min(xs)), math.floor(min(ys)),
		math.ceil(max(xs)) - math.floor(min(xs)),
		math.ceil(max(ys)) - math.floor(min(ys)))
	painter.path([("M",) + points[0], ("L",) + points[1], ("L",) + points[2],
		("Z",)], box, color)


def half_disc(painter, cx, cy, r, side, top, paint):
	"""A quarter of a disc, used for the round ends of slider grooves:
	\a side is "left" or "right", \a top whether it is the upper end."""
	sx = -1 if side == "left" else 1
	sy = -1 if top else 1
	start = (cx + sx * r, cy)
	end = (cx, cy + sy * r)
	sweep = 1 if sx * sy > 0 else 0
	box = (min(cx, cx + sx * r), min(cy, cy + sy * r), r, r)
	box = (math.floor(box[0]), math.floor(box[1]),
		math.ceil(box[0] + r) - math.floor(box[0]),
		math.ceil(box[1] + r) - math.floor(box[1]))
	painter.path([("M", cx, cy), ("L",) + start, ("A", r, sweep) + end,
		("Z",)], box, paint)


def rounded_corner(painter, x, y, r, corner, paint):
	"""A quarter disc filling the corner square at x, y of side r:
	corner is "topleft" or "topright"."""
	if corner == "topleft":
		cmd = [("M", x + r, y + r), ("L", x, y + r), ("A", r, 1, x + r, y),
			("Z",)]
	else:
		cmd = [("M", x, y + r), ("L", x, y), ("A", r, 1, x + r, y + r),
			("Z",)]
	painter.path(cmd, (x, y, r, r), paint)


def rounded_top(painter, rect, r, paint):
	"""A rectangle with its top corners rounded by r: two quarter discs and
	rectangles, so each corner stays within its slice."""
	x, y, w, h = rect
	rounded_corner(painter, x, y, r, "topleft", paint)
	rounded_corner(painter, x + w - r, y, r, "topright", paint)
	painter.fill(x + r, y, w - 2 * r, r, paint)
	painter.fill(x, y + r, w, h - r, paint)


# the two looks

class Look:
	"""What both looks share; the subclasses draw the parts."""

	def __init__(self):
		self.sheet = Sheet()
		# the frame widths each part was cut with, for the configuration
		self.widths = {}

	def stateful(self, frame, interior, widths, draw, states=STATES):
		"""Adds a sliced part in each state; draw(state) paints it, and
		disabled parts are the normal ones faded, unless draw handles it."""
		self.widths[frame or interior] = widths
		for state in states:
			painter = draw(state)
			if painter is None:
				painter = draw("normal").faded(self.panel, 0.5)
			self.sheet.nine(frame, interior, state, painter, widths)

	def whole(self, name, draw, states=STATES):
		for state in states:
			painter = draw(state)
			if painter is None:
				painter = draw("normal").faded(self.panel, 0.5)
			self.sheet.add("%s-%s" % (name, state), painter)

	def invisible(self, name, w=4, h=4):
		self.sheet.add(name, blank(w, h))

	def build(self):
		self.buttons()
		self.checks()
		self.fields()
		self.scroll_bars()
		self.sliders()
		self.progress()
		self.tabs()
		self.menus()
		self.views()
		self.arrows()
		self.misc()
		return self.sheet

	# parts that are the same in both looks, but for colours

	def focus_line(self):
		# keyboard focus: an underline in the navigation colour, as the
		# control looks mark labels
		for piece in ("top", "left", "right", "topleft", "topright"):
			self.invisible("focus-" + piece, 2, 2)
		for piece in ("bottom", "bottomleft", "bottomright"):
			p = Painter(2, 1)
			p.fill(0, 0, 2, 1, self.navigation)
			self.sheet.add("focus-" + piece, p)

	def menu_marks(self):
		# BMenuItem marks a chosen item with a tick, whether it is a check
		# box or one of a group; there is no box
		size = min(16, self.check_size)

		def tick(state, mark):
			p = Painter(size, size)
			if mark is None:
				return p
			color = self.text if state != "disabled" else tint(self.menu, 1.3)
			if mark == "tristate":
				p.fill(round(size * 0.25), size // 2 - 1, size - 2 * round(
					size * 0.25), 2, color)
				return p
			s = size
			lines(p, color, [(s * 0.2, s * 0.52), (s * 0.42, s * 0.75),
				(s * 0.8, s * 0.27)], 2)
			return p
		for prefix, mark in (("menu-checkbox", None),
				("menu-checkbox-checked", "check"),
				("menu-checkbox-tristate", "tristate"), ("menu-radio", None),
				("menu-radio-checked", "check")):
			self.whole(prefix, lambda st, m=mark: tick(st, m))

	def separator_lines(self, name, base, w, h):
		# BSeparatorItem: a dark line over a light one
		p = Painter(w, h)
		middle = h // 2 - 1
		p.fill(0, middle, w, 1, tint(base, 1.147))
		p.fill(0, middle + 1, w, 1, tint(base, 0.385))
		self.sheet.add(name, p)

	def progress(self):
		# Haiku's DrawStatusBar(), which Gonx keeps
		base = self.panel
		bar = self.status_bar
		edge_dark = tint(base, 1.0735)
		edge_light = tint(tint(base, 0.6), 0.59)

		def groove(state):
			p = Painter(10, 10)
			r = p.frame((0, 0, 10, 10), edge_dark, edge_dark, edge_light,
				edge_light)
			r = p.frame(r, tint(base, 1.3), tint(base, 1.3), tint(base, 1.2),
				tint(base, 1.2))
			p.fill(r[0], r[1], r[2], r[3], vertical(0, 2, 6,
				[(0, tint(base, 0.25)), (1, tint(base, 0.06))]))
			return p
		self.stateful("progress", "progress", (2, 2, 2, 2), groove,
			("normal",))

		def contents(state):
			color = bar if state != "disabled" else mix(bar, base, 0.6)
			p = Painter(10, 10)
			r = p.frame((0, 0, 10, 10), edge_dark, edge_dark, edge_light,
				edge_light)
			r = p.frame(r, tint(color, 1.2), tint(color, 1.2),
				tint(color, 1.45), tint(color, 1.45))
			p.fill(r[0], r[1], r[2], r[3], vertical(0, 2, 6, [
				(0, tint(color, 0.55)), (132 / 255, tint(color, 0.68)),
				(136 / 255, tint(color, 0.76)), (1, tint(color, 0.90))]))
			return p
		self.stateful("progress-pattern", "progress-pattern", (2, 2, 2, 2),
			contents, ("normal", "focused", "disabled"))

	def misc(self):
		base = self.panel
		self.focus_line()
		self.menu_marks()
		self.invisible("button-default-indicator")
		self.invisible("flat-button-default-indicator")

		# tool bars are plain panels, with a dotted handle and a line
		p = Painter(4, 4)
		p.fill(0, 0, 4, 4, base)
		self.sheet.add("toolbar-normal", p)
		self.sheet.add("toolbar-disabled", p)
		p = Painter(4, 12)
		for y in range(1, 12, 3):
			p.fill(1, y, 1, 1, tint(base, 1.3))
			p.fill(2, y + 1, 1, 1, WHITE)
		self.sheet.add("toolbar-handle", p)
		p = Painter(4, 8)
		p.fill(1, 0, 1, 8, tint(base, 1.147))
		p.fill(2, 0, 1, 8, tint(base, 0.385))
		self.sheet.add("toolbar-separator", p)

		# splitters: the panel with Haiku's grip dots
		def splitter(state):
			p = Painter(4, 4)
			p.fill(0, 0, 4, 4, base)
			return p
		self.whole("splitter", splitter, ("normal", "focused", "pressed",
			"disabled"))

		def grip(state):
			p = Painter(6, 16)
			for y in range(1, 16, 5):
				p.fill(2, y, 2, 2, tint(base, 1.29))
				p.fill(3, y + 1, 2, 2, tint(base, 0.6))
				p.fill(3, y + 1, 1, 1, tint(base, 1.29))
			return p
		self.whole("splitter-grip", grip, ("normal", "focused", "pressed",
			"disabled"))

		# the window resize grip, a few dots in the bottom right corner
		def size_grip(state):
			p = Painter(12, 12)
			for i in range(3):
				for j in range(3 - i):
					x = 9 - 4 * i
					y = 9 - 4 * j
					p.fill(x, y, 2, 2, tint(base, 1.29))
					p.fill(x + 1, y + 1, 1, 1, WHITE)
			return p
		self.whole("sizegrip", size_grip, ("normal", "focused"))

		# tool tips
		def tooltip(state):
			p = Painter(8, 8)
			p.frame((0, 0, 8, 8), *[tint(self.tooltip, 1.45)] * 4)
			p.fill(1, 1, 6, 6, self.tooltip)
			return p
		self.stateful("tooltip", "tooltip", (1, 1, 1, 1), tooltip,
			("normal",))

		# dock widgets and their titles
		def dock(state):
			p = Painter(8, 8)
			p.frame((0, 0, 8, 8), *[tint(base, 1.3)] * 4)
			p.fill(1, 1, 6, 6, base)
			return p
		self.stateful("dock", "dock", (1, 1, 1, 1), dock, ("normal",))

		def dock_title(state):
			p = Painter(8, 8)
			p.fill(0, 0, 8, 8, vertical(0, 0, 8,
				[(0, tint(base, 0.6)), (1, tint(base, 1.03))]))
			p.fill(0, 7, 8, 1, tint(base, 1.3))
			return p
		self.whole("docktitle", dock_title, ("normal", "focused"))

		# MDI title bars, rarely seen: the yellow tab when active
		def title_bar(state):
			p = Painter(8, 8)
			color = rgb("#ffcb00") if state == "focused" else rgb("#e8e8e8")
			p.fill(0, 0, 8, 8, vertical(0, 0, 8,
				[(0, tint(color, 0.6)), (1, color)]))
			return p
		self.whole("titlebar", title_bar, ("normal", "focused"))

		for kind in ("close", "maximize", "restore", "minimize", "shade"):
			def mdi(state, kind=kind):
				p = Painter(12, 12)
				color = self.text if state != "disabled" else tint(base, 1.3)
				if kind == "close":
					round_line(p, color, (3, 3), (9, 9), 1.5)
					round_line(p, color, (3, 9), (9, 3), 1.5)
				elif kind == "minimize":
					p.fill(3, 8, 6, 2, color)
				elif kind == "shade":
					p.fill(3, 3, 6, 2, color)
				else:
					p.frame((2, 2, 8, 8), *[color] * 4)
					p.fill(3, 3, 6, 1, color)
				return p
			self.whole("mdi-" + kind, mdi, ("normal", "focused", "pressed",
				"disabled"))
		p = Painter(12, 12)
		p.fill(2, 4, 8, 2, self.text)
		self.sheet.add("mdi-menu-normal", p)

		# dials: a plain ring, notches and a handle
		p = Painter(32, 32)
		p.ellipse(0, 0, 32, 32, tint(base, 1.3))
		p.ellipse(1, 1, 30, 30, vertical(0, 1, 30,
			[(0, tint(base, 0.4)), (1, tint(base, 1.05))]))
		self.sheet.add("dial", p)
		p = Painter(32, 32)
		p.path(ellipse_path(1, 1, 30, 30), (0, 0, 32, 32), "butt",
			stroke=(1, tint(base, 1.3)))
		self.sheet.add("dial-notches", p)
		p = Painter(8, 8)
		p.ellipse(0, 0, 8, 8, self.mark)
		self.sheet.add("dial-handle", p)

		# the close button of tabs
		def close(state):
			p = Painter(12, 12)
			color = (self.text if state in ("normal", "toggled")
				else self.mark if state != "disabled" else tint(base, 1.3))
			round_line(p, color, (3.5, 3.5), (8.5, 8.5), 1.5)
			round_line(p, color, (3.5, 8.5), (8.5, 3.5), 1.5)
			return p
		self.whole("tab-close", close)


class HaikuLook(Look):
	"""Haiku's own control look, HaikuControlLook, in its default colours."""

	name = "Haiku"

	def __init__(self):
		super().__init__()
		self.panel = (216, 216, 216)
		self.menu = (216, 216, 216)
		self.menu_selected = (153, 153, 153)
		self.control = (222, 222, 222)
		self.control_border = (172, 172, 172)
		self.mark = (27, 82, 140)
		self.navigation = (0, 0, 229)
		self.document = WHITE
		self.list_selected = (190, 190, 190)
		self.tooltip = (255, 255, 216)
		self.status_bar = (50, 150, 255)
		self.text = BLACK

	# buttons: HaikuControlLook::_DrawButtonFrame() and
	# _DrawNonFlatButtonBackground(), three rings around a glossy face

	def button(self, state, default=False, focus=False):
		bg, base = self.panel, self.control
		pressed = state in ("pressed", "toggled")
		disabled = state == "disabled"
		p = Painter(14, 22)
		r = (0, 0, 14, 22)

		if focus:
			# keyboard focus turns the frame ring blue; Kvantum lays this
			# over the button
			p.frame((1, 1, 12, 20), *[self.navigation] * 4)
			return p

		# the outer edge, recessed into the panel; a default button's is its
		# dark ring instead, as there is no room for both
		if default:
			r = p.frame(r, *[self.control_border] * 4)
		elif disabled:
			r = p.frame(r, *[bg] * 4)
		else:
			r = p.frame(r, tint(bg, 1.0735), tint(bg, 1.0735),
				tint(bg, 0.59), tint(bg, 0.59))

		# the frame
		if disabled:
			light, shadow = tint(base, 1.145), tint(base, 1.26)
		elif default:
			light = tint(tint(base, 1.35), 1.35)
			shadow = tint(tint(base, 1.33), 1.5)
		else:
			light, shadow = tint(base, 1.35), tint(base, 1.485)
		if pressed:
			light, shadow = shadow, light
		r = p.frame(r, light, light, shadow, shadow)

		# the bevel, with its own colour where light meets shadow
		if pressed:
			bevel_light = bevel_shadow = tint(base, 1.17)
		elif disabled:
			bevel_light, bevel_shadow = tint(base, 0.59), base
		else:
			bevel_light, bevel_shadow = tint(base, 0.2), tint(base, 1.105)
		corner = tint(base, 0.84 if disabled else 0.7)
		x, y, w, h = r
		p.fill(x, y, 1, h, bevel_light)
		p.fill(x, y, w, 1, bevel_light)
		p.fill(x + w - 1, y, 1, h, bevel_shadow)
		p.fill(x, y + h - 1, w, 1, bevel_shadow)
		if not pressed:
			p.fill(x, y + h - 1, 1, 1, corner)
			p.fill(x + w - 1, y, 1, 1, corner)
		p.fill(x + 1, y + 1, w - 2, h - 2,
			self.face_gradient(base, state, vertical(0, y + 1, h - 2, [])))
		return p

	def face_gradient(self, base, state, where):
		"""_MakeButtonGradient()"""
		tints = [0.6, 0.75, 0.9, 1.01]
		if state in ("pressed", "toggled"):
			tints = [1.135, 1.135, 1.105, 1.105]
		elif state == "disabled":
			tints = [(t + 1) / 2 for t in tints]
		elif state == "focused":
			tints = [t * 0.85 for t in tints]
		offsets = [0, 132 / 255, 136 / 255, 1]
		return Gradient(where.x1, where.y1, where.x2, where.y2,
			[(o, tint(base, t)) for o, t in zip(offsets, tints)])

	def buttons(self):
		self.stateful("button", "button", (3, 3, 3, 3), self.button)
		self.sheet.nine("button", None, "default",
			self.button("normal", default=True), (3, 3, 3, 3))
		self.sheet.nine("button", None, "focus",
			self.button("normal", focus=True), (3, 3, 3, 3))

		# the pop-up marker of menu fields: a dark triangle on a darker
		# strip of the face, past a line
		def marker(state):
			p = Painter(16, 16)
			base = self.control
			p.fill(0, 0, 1, 16, tint(base, 1.105))
			color = tint(base, 1.35 if state == "disabled" else 1.65)
			triangle(p, [(5.5, 6.5), (11.5, 6.5), (8.5, 10)], color)
			return p
		self.whole("carrow", marker)

		# spinner buttons are small buttons with a minus and a plus
		for name, plus in (("up", True), ("down", False), ("plus", True),
				("minus", False)):
			def glyph(state, plus=plus):
				p = Painter(9, 9)
				color = self.text if state != "disabled" else tint(self.panel,
					1.3)
				p.fill(0, 4, 9, 1, color)
				if plus:
					p.fill(4, 0, 1, 9, color)
				return p
			self.whole("spin-" + name, glyph)

	# check boxes and radio buttons: DrawCheckBox(), DrawRadioButton()

	def check_box(self, state, mark):
		base = self.panel
		size = 17
		p = Painter(size, size)
		disabled = state == "disabled"
		clicked = state == "pressed"
		r = (0, 0, size, size)
		if clicked:
			dark1, dark2 = tint(base, 1.5), tint(base, 1.48)
			r = p.frame(r, dark1, dark1, dark2, dark2)
			dark2 = dark1
		else:
			if disabled:
				r = p.frame(r, *[base] * 4)
				dark1 = dark2 = tint(base, 1.15)
			else:
				r = p.frame(r, tint(base, 1.0735), tint(base, 1.0735),
					tint(base, 0.59), tint(base, 0.59))
				dark1, dark2 = tint(base, 1.40), tint(base, 1.38)
		r = p.frame(r, dark1, dark1, dark2, dark2)
		top, bottom = (0.4, 0.2) if disabled else (0.15, 0.0)
		p.fill(*r, vertical(0, r[1], r[3],
			[(0, tint(base, top)), (1, tint(base, bottom))]))
		if mark is None:
			return p

		color = self.mark
		if disabled:
			color = mix(base, self.mark, 0.4)
		elif clicked:
			color = mix(base, self.mark, 0.7)
		# inset by the font size / 6 in the frame's inside, a pen a third
		# of what is left
		x, y, w, h = r
		inset = 2
		left, top_ = x + inset, y + inset
		right, bottom_ = x + w - 1 - inset, y + h - 1 - inset
		pen = max(1, math.ceil((right - left) / 3.5))
		if mark == "tristate":
			middle = (top_ + bottom_) / 2 + 0.5
			p.fill(left, int(middle - pen / 2), right - left + 1, pen, color)
			return p
		box = (0, 0, size, size)
		for a, b in (((left + 0.5, top_ + 0.5), (right + 0.5, bottom_ + 0.5)),
				((left + 0.5, bottom_ + 0.5), (right + 0.5, top_ + 0.5))):
			p.path([("M",) + a, ("L",) + b], box, "butt",
				stroke=(pen - 0.5, color))
		return p

	def radio_button(self, state, checked):
		base = self.panel
		size = 17
		p = Painter(size, size)
		disabled = state == "disabled"
		clicked = state == "pressed"
		if disabled:
			border = tint(base, 1.15)
			light = shadow = base
		elif clicked:
			border = light = shadow = tint(base, 1.5)
		else:
			border = tint(base, 1.45)
			light, shadow = tint(base, 0.55), tint(base, 1.11)
		p.ellipse(0, 0, size, size,
			Gradient(0, 0, size, size, [(0, shadow), (1, light)]))
		p.ellipse(1, 1, size - 2, size - 2, Gradient(1, 1, 1, size - 1,
			[(0, border), (1, tint(border, 0.8))]))
		top, bottom = (0.4, 0.2) if disabled else (0.15, 0.0)
		p.ellipse(2, 2, size - 4, size - 4, vertical(0, 2, size - 4,
			[(0, tint(base, top)), (1, tint(base, bottom))]))
		if checked:
			color = self.mark
			if disabled:
				color = mix(base, self.mark, 0.4)
			elif clicked:
				color = mix(base, self.mark, 0.7)
			p.ellipse(5, 5, size - 10, size - 10, color)
		return p

	def checks(self):
		for mark, prefix in ((None, "checkbox"), ("check", "checkbox-checked"),
				("tristate", "checkbox-tristate")):
			self.whole(prefix, lambda s, m=mark: self.check_box(
				"normal" if s == "focused" else s, m))
		for checked, prefix in ((False, "radio"), (True, "radio-checked")):
			self.whole(prefix, lambda s, c=checked: self.radio_button(
				"normal" if s == "focused" else s, c))

	# text fields, group frames, view frames

	def fields(self):
		bg = self.panel

		def line_edit(state):
			p = Painter(12, 12)
			r = (0, 0, 12, 12)
			if state == "disabled":
				r = p.frame(r, *[bg] * 4)
				r = p.frame(r, *[tint(bg, 1.15)] * 4)
			else:
				r = p.frame(r, tint(bg, 1.0735), tint(bg, 1.0735),
					tint(bg, 0.59), tint(bg, 0.59))
				if state == "focused":
					r = p.frame(r, *[self.navigation] * 4)
				else:
					r = p.frame(r, tint(bg, 1.40), tint(bg, 1.40),
						tint(bg, 1.38), tint(bg, 1.38))
			p.fill(*r, self.document if state != "disabled"
				else mix(self.document, bg, 0.5))
			return p
		self.stateful("lineedit", "lineedit", (2, 2, 2, 2), line_edit,
			("normal", "focused", "disabled"))

		# DrawGroupFrame(): a dark line between two bevels
		def group(state):
			p = Painter(10, 10)
			light, shadow = tint(bg, 0.8), tint(bg, 1.03)
			r = p.frame((0, 0, 10, 10), shadow, shadow, light, light)
			r = p.frame(r, *[tint(bg, 1.30)] * 4)
			p.frame(r, light, light, shadow, shadow)
			return p
		self.stateful("group", None, (3, 3, 3, 3), group, ("normal",))

		# DrawScrollViewFrame(), B_FANCY_BORDER: recessed, with a dark line
		def view_frame(state):
			p = Painter(8, 8)
			r = p.frame((0, 0, 8, 8), tint(bg, 1.0735), tint(bg, 1.0735),
				tint(bg, 0.59), tint(bg, 0.59))
			color = self.navigation if state == "focused" else tint(bg, 1.29)
			p.frame(r, *[color] * 4)
			return p
		self.stateful("common", None, (2, 2, 2, 2), view_frame,
			("normal", "focused"))

	# scroll bars: drawn vertical, Kvantum turns them for horizontal ones

	def scroll_button_face(self, p, rect, state):
		# DrawButtonBackground() in the scroll bar's colours
		base = self.panel
		x, y, w, h = rect
		pressed = state == "pressed"
		disabled = state == "disabled"
		if pressed:
			light = shadow = tint(base, 1.17)
		elif disabled:
			light, shadow = tint(base, 0.59), base
		else:
			light, shadow = tint(base, 0.2), tint(base, 1.105)
		p.fill(x, y, 1, h, light)
		p.fill(x, y, w, 1, light)
		p.fill(x + w - 1, y, 1, h, shadow)
		p.fill(x, y + h - 1, w, 1, shadow)
		if not pressed:
			corner = tint(base, 0.84 if disabled else 0.7)
			p.fill(x, y + h - 1, 1, 1, corner)
			p.fill(x + w - 1, y, 1, 1, corner)
		face_state = "disabled" if disabled else "pressed" if pressed else ""
		p.fill(x + 1, y + 1, w - 2, h - 2, self.face_gradient(
			base if not disabled else tint(base, 0.59), face_state,
			horizontal(x + 1, 0, w - 2, [])))

	def scroll_bars(self):
		size = self.scroll_width
		base = self.panel
		border = tint(base, 1.29)

		def arrow_button(state, up):
			p = Painter(size, size)
			# the up button has the scroll bar's border at the top and the
			# groove's dark edge at the bottom, the down button the reverse
			p.fill(0, 0, size, size, border)
			self.scroll_button_face(p, (1, 1, size - 2, size - 2), state)
			color = self.text if state != "disabled" else tint(self.text,
				0.59)
			# DrawArrowShape(): its thick pen fills the small chevron in, so
			# at this size it reads as a solid triangle
			c = size / 2
			if up:
				points = [(c - 3, c + 2), (c, c - 2), (c + 3, c + 2)]
			else:
				points = [(c - 3, c - 2), (c, c + 2), (c + 3, c - 2)]
			triangle(p, points, color)
			return p

		for direction in ("up", "down", "left", "right"):
			up = direction in ("up", "left")
			self.whole("sarrow-" + direction,
				lambda s, up=up: arrow_button(
					"normal" if s in ("focused", "toggled") else s, up))

		# the groove: DrawScrollBarBackground(), a shadow line after the
		# dark edge the buttons end with, then a gradient across
		def groove(state):
			p = Painter(size, 12)
			p.fill(0, 0, size, 12, border)
			p.fill(1, 0, size - 2, 1, tint(base, 1.10))
			p.fill(1, 1, size - 2, 11, horizontal(1, 0, size - 2,
				[(0, tint(base, 1.10)), (1, tint(base, 1.05))]))
			return p
		self.stateful("sgroove", "sgroove", (1, 1, 1, 0), groove, ("normal",))

		# the thumb is a button face between the scroll bar's borders
		def thumb(state):
			p = Painter(size, 10)
			p.fill(0, 0, size, 10, border)
			self.scroll_button_face(p, (1, 0, size - 2, 10),
				"normal" if state == "focused" else state)
			return p
		self.stateful("sslider", "sslider", (2, 1, 2, 1), thumb,
			("normal", "focused", "pressed", "disabled"))

		# no knob: Hokku's scroll bars are plain (B_KNOB_NONE)
		for state in ("normal", "focused", "pressed", "disabled"):
			self.invisible("sgrip-" + state, 3, 3)

	# sliders: drawn vertical too

	def sliders(self):
		base = self.panel
		width = self.slider_width
		fill = tint(base, 1.147)

		def bar(state):
			disabled = state == "disabled"
			color = fill if not disabled else mix(fill, base, 0.6)
			if disabled:
				edge_light, edge_shadow = base, base
				frame_light, frame_shadow = tint(color, 1.2), tint(color, 1.25)
				fill_light, fill_shadow = tint(color, 0.9), tint(color, 1.05)
			else:
				edge_light, edge_shadow = tint(base, 0.65), tint(base, 1.07)
				frame_light, frame_shadow = tint(color, 1.4), tint(color, 1.5)
				fill_light, fill_shadow = tint(color, 0.8), tint(color, 1.1)
			p = Painter(width, 16)
			# straight part: edge, frame, gradient, frame, edge across
			p.fill(0, 4, 1, 8, edge_shadow)
			p.fill(1, 4, 1, 8, frame_shadow)
			p.fill(2, 4, width - 4, 8, horizontal(2, 0, width - 4,
				[(0, fill_shadow), (1, fill_light)]))
			p.fill(width - 2, 4, 1, 8, frame_light)
			p.fill(width - 1, 4, 1, 8, edge_light)
			# round ends, shadow on the left half and light on the right
			c = width / 2
			for top, cy in ((True, 4), (False, 12)):
				half_disc(p, c, cy, c, "left", top, edge_shadow)
				half_disc(p, c, cy, c, "right", top, edge_light)
				half_disc(p, c, cy, c - 1, "left", top, frame_shadow)
				half_disc(p, c, cy, c - 1, "right", top, frame_light)
				half_disc(p, c, cy, c - 2, "left", top, fill_shadow)
				half_disc(p, c, cy, c - 2, "right", top, fill_light)
			return p
		self.stateful("slider", "slider", (0, 4, 0, 4), bar,
			("normal", "toggled", "disabled"))

		# the hash marks
		p = Painter(5, 1)
		p.fill(0, 0, 5, 1, tint(base, 1.29))
		self.sheet.add("slider-tick-normal", p)

		# DrawSliderThumb(): a framed button face with a drop shadow and
		# a short groove across the middle
		across, along = self.slider_handle
		control = self.control

		def thumb(state):
			disabled = state == "disabled"
			p = Painter(across, along)
			shadow_alpha = (30 if disabled else 60) / 255
			p.fill(across - 1, 1, 1, along - 2, BLACK, shadow_alpha)
			p.fill(1, along - 1, across - 1, 1, BLACK, shadow_alpha)
			light = tint(control, 1.30 if disabled else 1.6)
			shadow = tint(control, 1.35 if disabled else 1.65)
			r = p.frame((0, 0, across - 1, along - 1), light, light, shadow,
				shadow)
			x, y, w, h = r
			bevel_light = tint(control, 0.59 if disabled else 0.2)
			bevel_shadow = control if disabled else tint(control, 1.105)
			p.fill(x, y, 1, h, bevel_light)
			p.fill(x, y, w, 1, bevel_light)
			p.fill(x + w - 1, y, 1, h, bevel_shadow)
			p.fill(x, y + h - 1, w, 1, bevel_shadow)
			corner = tint(control, 0.84 if disabled else 0.7)
			p.fill(x, y + h - 1, 1, 1, corner)
			p.fill(x + w - 1, y, 1, 1, corner)
			p.fill(x + 1, y + 1, w - 2, h - 2, self.face_gradient(control,
				state if state in ("focused", "disabled") else "normal",
				horizontal(x + 1, 0, w - 2, [])))
			middle = y + (h - 1) // 2
			inset = (w - 1) // 4
			p.fill(x + inset, middle, w - 2 * inset, 1, tint(control, 1.29),
				0.5)
			p.fill(x + inset, middle + 1, w - 2 * inset, 1,
				tint(control, 0.385), 0.5)
			return p
		self.whole("slidercursor", lambda s: thumb(
			"normal" if s in ("pressed", "toggled") else s))

	# tabs, drawn on top of their content

	def tabs(self):
		bg = self.panel
		line = tint(bg, 1.3)
		top_row = self.tab_offset

		def tab(state):
			active = state in ("toggled", "pressed")
			p = Painter(12, top_row + 2 + 12 + 1)
			h = p.height
			if active:
				# DrawActiveTab(): raised, light, with rounded corners
				y = 0
				p.fill(0, y, 12, 1, tint(bg, 1.03))
				p.fill(1, y, 10, 1, line)
				p.fill(0, y + 1, 1, h - 1, line)
				p.fill(11, y + 1, 1, h - 1, line)
				p.fill(1, y + 1, 10, 1, tint(bg, 0.6))
				p.fill(1, y + 2, 1, h - 2, tint(bg, 0.6))
				p.fill(10, y + 2, 1, h - 2, tint(bg, 1.0735))
				p.fill(2, y + 2, 8, h - 2, vertical(0, y + 2, h - 2, [
					(0, tint(bg, 0.7)), (1, tint(bg, 1.03))]))
				return p
			# DrawInactiveTab(): lower and darker, joined to the next one
			y = top_row
			p.fill(0, y, 12, 1, tint(bg, 1.03))
			p.fill(0, y + 1, 12, 1, line)
			p.fill(0, y + 2, 1, h - y - 2, tint(bg, 0.6))
			p.fill(11, y + 2, 1, h - y - 2, line)
			face = (1.07, 1.03) if state != "focused" else (1.03, 0.97)
			p.fill(1, y + 2, 10, h - y - 2, vertical(0, y + 2, h - y - 2, [
				(0, tint(bg, face[0] + 0.05)), (1, tint(bg, face[1] + 0.05))]))
			return p
		self.stateful("tab", "tab", (2, top_row + 2, 2, 1), tab,
			("normal", "focused", "toggled", "disabled"))

		# the tab view's frame: a dark line with a light bevel inside
		def tab_frame(state):
			p = Painter(8, 8)
			r = p.frame((0, 0, 8, 8), *[line] * 4)
			p.frame(r, tint(bg, 0.8), tint(bg, 0.8), tint(bg, 1.03),
				tint(bg, 1.03))
			return p
		self.stateful("tabframe", None, (2, 2, 2, 2), tab_frame, ("normal",))

	# menus

	def menu_item_background(self, w, h, base, edge_tints=(0.9, 1.05)):
		# DrawMenuItemBackground(), activated
		p = Painter(w, h)
		light, shadow = tint(base, edge_tints[0]), tint(base, edge_tints[1])
		r = p.frame((0, 0, w, h), light, light, shadow, shadow)
		p.fill(*r, vertical(0, 1, h - 2, [(0, shadow), (1, light)]))
		return p

	def menus(self):
		base = self.menu

		# DrawMenuBackground(): a bevel
		def menu(state):
			p = Painter(8, 8)
			r = p.frame((0, 0, 8, 8), tint(base, 0.6), tint(base, 0.6),
				tint(base, 1.12), tint(base, 1.12))
			p.fill(*r, base)
			return p
		self.stateful("menu", "menu", (1, 1, 1, 1), menu, ("normal",))

		def item(state):
			if state in ("focused", "pressed", "toggled"):
				return self.menu_item_background(8, 8, self.menu_selected)
			return blank(8, 8)
		self.stateful("menuitem", "menuitem", (1, 1, 1, 1), item,
			("normal", "focused", "pressed", "toggled"))
		self.stateful("menubaritem", "menubaritem", (1, 1, 1, 1), item,
			("normal", "focused", "pressed", "toggled"))

		# DrawMenuBarBackground()
		def menu_bar(state):
			p = Painter(8, 8)
			r = p.frame((0, 0, 8, 8), tint(base, 0.7), tint(base, 0.5),
				tint(base, 1.08), tint(base, 1.08))
			p.fill(0, 0, 1, 1, tint(base, 0.9))
			p.fill(*r, vertical(0, 1, 6,
				[(0, tint(base, 0.69)), (1, tint(base, 1.03))]))
			return p
		self.stateful("menubar", "menubar", (1, 1, 1, 1), menu_bar,
			("normal",))

		self.separator_lines("mitem-separator", base, 8, 6)

		# the submenu marker
		for direction in ("left", "right"):
			def marker(state, direction=direction):
				p = Painter(9, 9)
				color = self.text if state != "disabled" else tint(base, 1.3)
				if direction == "right":
					triangle(p, [(2.5, 1), (7, 4.5), (2.5, 8)], color)
				else:
					triangle(p, [(6.5, 1), (2, 4.5), (6.5, 8)], color)
				return p
			self.whole("mitem-" + direction, marker, ("normal", "focused",
				"disabled"))
		self.invisible("mitem-tearoff-normal")
		self.invisible("mitem-tearoff-focused")

	# item views and their headers

	def views(self):
		def item(state):
			p = Painter(6, 6)
			if state in ("pressed", "toggled"):
				p.fill(0, 0, 6, 6, self.list_selected)
			return p
		self.stateful("itemview", "itemview", (1, 1, 1, 1), item,
			("normal", "focused", "pressed", "toggled", "disabled"))

		# BColumnListView's title bar: a lit gradient with dark lines
		bg = self.panel

		def header(state):
			p = Painter(8, 12)
			tints = (0.5, 1.03)
			if state == "pressed":
				tints = (1.08, 1.0)
			elif state == "focused":
				tints = (0.4, 0.95)
			p.fill(0, 0, 8, 12, vertical(0, 0, 12,
				[(0, tint(bg, tints[0])), (1, tint(bg, tints[1]))]))
			p.fill(0, 0, 8, 1, tint(bg, 0.6))
			p.fill(0, 11, 8, 1, tint(bg, 1.3))
			p.fill(7, 0, 1, 11, tint(bg, 1.3))
			return p
		self.stateful("header", "header", (1, 1, 1, 1), header,
			("normal", "focused", "pressed", "disabled"))
		p = Painter(1, 8)
		p.fill(0, 0, 1, 8, tint(bg, 1.3))
		self.sheet.add("header-separator", p)

	# arrows, for tool buttons, headers, combo boxes and tree branches

	def arrows(self):
		bg = self.panel

		def arrow(state, direction):
			p = Painter(9, 9)
			color = self.text
			if state == "disabled":
				color = tint(bg, 1.3)
			elif state == "focused":
				color = self.mark
			points = {
				"up": [(1, 6.5), (4.5, 3), (8, 6.5)],
				"down": [(1, 2.5), (4.5, 6), (8, 2.5)],
				"left": [(6.5, 1), (3, 4.5), (6.5, 8)],
				"right": [(2.5, 1), (6, 4.5), (2.5, 8)],
			}[direction]
			lines(p, color, points, 2)
			return p
		for direction in ("up", "down", "left", "right"):
			self.whole("arrow-" + direction, lambda s, d=direction:
				arrow(s, d))

		# BOutlineListView's latches: filled triangles, right when closed
		def latch(state, expanded):
			p = Painter(9, 9)
			color = tint(bg, 1.6) if state != "disabled" else tint(bg, 1.3)
			if state == "focused":
				color = self.mark
			if expanded:
				triangle(p, [(0.5, 2.5), (8.5, 2.5), (4.5, 7)], color)
			else:
				triangle(p, [(2.5, 0.5), (7, 4.5), (2.5, 8.5)], color)
			return p
		self.whole("tree-plus", lambda s: latch(s, False),
			("normal", "focused", "pressed", "disabled"))
		self.whole("tree-minus", lambda s: latch(s, True),
			("normal", "focused", "pressed", "disabled"))


class GonxLook(Look):
	"""GonxControlLook in the Gonx theme's colours."""

	name = "Gonx"

	def __init__(self):
		super().__init__()
		self.panel = rgb("#ecebe6")
		self.menu = rgb("#f7f7f5")
		self.menu_selected = rgb("#9eb2ca")
		self.control = (222, 222, 222)
		self.mark = (59, 82, 118)
		self.navigation = rgb("#4f6c97")
		self.document = WHITE
		self.list_selected = rgb("#aabbd0")
		self.tooltip = (255, 255, 216)
		self.status_bar = (50, 150, 255)
		self.thumb = (216, 216, 216)
		self.text = BLACK

		self.border = (150, 149, 142)
		self.outline = (148, 147, 141)
		self.outline_pressed = (110, 109, 103)
		self.default_outline = (109, 118, 147)
		self.default_glow = (143, 163, 184)
		self.default_light = (214, 224, 238)
		self.arrow_color = (126, 125, 119)
		self.face = (247, 247, 242)

	def shadow(self, background):
		return mix(background, BLACK, 0.2)

	def face_color(self, state):
		if state in ("pressed", "toggled"):
			return mix(self.control, BLACK, 0.12)
		if state == "focused":
			return mix(self.face, self.default_light, 0.5)
		return self.face

	# buttons: _DrawGonxButtonFrame() and _DrawFace(), a two pixel outline
	# with cut corners and a drop shadow

	def button(self, state, default=False, focus=False):
		w, h = 16, 24
		p = Painter(w, h)
		shape = (1, 1, w - 2, h - 2)
		pressed = state in ("pressed", "toggled")
		radius = 4

		if focus:
			# the outline in the navigation colour
			outer = rounded_rows(shape, radius)
			inner = rounded_rows((3, 3, w - 6, h - 6), 1)
			p.spans(subtract_rows(outer, inner), self.navigation)
			return p

		outline = self.outline_pressed if pressed else self.outline
		shadow = self.shadow(self.panel)
		if default:
			outline, shadow = self.default_outline, self.default_glow
		p.spans(rounded_rows((2, 2, w - 2, h - 2), radius), shadow)
		p.spans(rounded_rows(shape, radius), outline)
		face = self.face_color(state)
		if default:
			p.spans(rounded_rows((3, 3, w - 6, h - 6), radius - 2), face)
			p.fill(4, h - 4, w - 8, 1, mix(face, self.default_light, 0.75))
			return p
		p.fill(3, 3, w - 6, h - 6, face)
		for x, y in ((3, 3), (w - 4, 3), (3, h - 4), (w - 4, h - 4)):
			p.fill(x, y, 1, 1, outline)
		return p

	def buttons(self):
		self.stateful("button", "button", (4, 4, 4, 4), self.button)
		self.sheet.nine("button", None, "default",
			self.button("normal", default=True), (4, 4, 4, 4))
		self.sheet.nine("button", None, "focus",
			self.button("normal", focus=True), (4, 4, 4, 4))

		# the menu field's box: square, a two pixel outline and a shadow
		def box(state):
			w, h = 12, 12
			p = Painter(w, h)
			outline = self.outline
			p.fill(1, 1, w - 1, h - 1, self.shadow(self.panel))
			p.fill(0, 0, w - 1, h - 1, outline)
			p.fill(2, 2, w - 5, h - 5, self.face_color(
				"normal" if state in ("pressed", "toggled") else state))
			return p
		self.stateful("combo", "combo", (2, 2, 3, 3), box)

		# _DrawIndicator(): a grey segment with an outlined triangle and a
		# divider facing the label
		def segment(state):
			s = 15
			p = Painter(s, s)
			p.fill(0, 0, s, s, rgb("#d5d5cb"))
			p.fill(0, 0, 1, s, rgb("#9b9b8f"))
			outline, inside = gonx_triangle((1, 0, s - 1, s), "down")
			pixels(p, outline, rgb("#95958b"))
			pixels(p, inside, rgb("#f0f0e6"))
			return p
		self.whole("carrow", segment)

		# the spinner's minus and plus: thin black lines
		for name, plus in (("up", True), ("down", False), ("plus", True),
				("minus", False)):
			def glyph(state, plus=plus):
				p = Painter(9, 9)
				color = self.text if state != "disabled" else mix(self.text,
					self.panel, 0.6)
				p.fill(0, 4, 9, 1, color)
				if plus:
					p.fill(4, 0, 1, 9, color)
				return p
			self.whole("spin-" + name, glyph)

	# check boxes and radio buttons: a white box with a drop shadow, the
	# concept's blue cross, a blue dot

	def check_box(self, state, mark):
		base = self.panel
		size = 13
		p = Painter(size, size)
		disabled = state == "disabled"
		if not disabled:
			p.fill(1, 1, size - 1, size - 1, self.shadow(base))
		border = self.outline
		if disabled:
			border = mix(self.outline, base, 0.5)
		elif state == "pressed":
			border = self.outline_pressed
		p.fill(0, 0, size - 1, size - 1, border)
		fill = WHITE
		if disabled:
			fill = mix(WHITE, base, 0.5)
		elif state == "pressed":
			fill = mix(WHITE, base, 0.35)
		p.fill(1, 1, size - 3, size - 3, fill)
		if mark is None:
			return p

		color = mix(self.mark, base, 0.55) if disabled else self.mark
		inner = size - 3
		inset = max(2, round(inner / 5))
		left = 1 + inset
		right = 1 + inner - 1 - inset + 1
		pen = max(1.5, inner / 6.5)
		if mark == "tristate":
			y = (left + right) / 2 + 0.5
			round_line(p, color, (left + 0.5, y), (right + 0.5, y), pen)
		else:
			round_line(p, color, (left + 0.5, left + 0.5),
				(right + 0.5, right + 0.5), pen)
			round_line(p, color, (left + 0.5, right + 0.5),
				(right + 0.5, left + 0.5), pen)
		return p

	def radio_button(self, state, checked):
		base = self.panel
		size = 13
		p = Painter(size, size)
		disabled = state == "disabled"
		if not disabled:
			p.ellipse(1, 1, size - 1, size - 1, self.shadow(base))
		border = self.outline
		if disabled:
			border = mix(self.outline, base, 0.5)
		elif state == "pressed":
			border = self.outline_pressed
		p.ellipse(0, 0, size - 1, size - 1, border)
		p.ellipse(1, 1, size - 3, size - 3,
			mix(WHITE, base, 0.5) if disabled else WHITE)
		if checked:
			color = mix(self.mark, base, 0.55) if disabled else self.mark
			p.ellipse(2, 2, size - 5, size - 5, color)
		return p

	def checks(self):
		for mark, prefix in ((None, "checkbox"), ("check", "checkbox-checked"),
				("tristate", "checkbox-tristate")):
			self.whole(prefix, lambda s, m=mark: self.check_box(
				"normal" if s == "focused" else s, m))
		for checked, prefix in ((False, "radio"), (True, "radio-checked")):
			self.whole(prefix, lambda s, c=checked: self.radio_button(
				"normal" if s == "focused" else s, c))

	# text fields, group frames, view frames

	def fields(self):
		bg = self.panel

		# DrawTextControlBorder(): a soft border with a shadow below
		def line_edit(state):
			p = Painter(12, 12)
			disabled = state == "disabled"
			if not disabled:
				p.fill(1, 1, 11, 11, self.shadow(bg))
			border = mix(self.outline, bg, 0.25)
			if disabled:
				border = mix(mix(self.outline, bg, 0.5), bg, 0.25)
			elif state == "focused":
				border = self.navigation
			p.fill(0, 0, 11, 11, border)
			p.fill(1, 1, 9, 9, self.document if not disabled
				else mix(self.document, bg, 0.5))
			return p
		self.stateful("lineedit", "lineedit", (1, 1, 2, 2), line_edit,
			("normal", "focused", "disabled"))

		# DrawGroupFrame(): one grey line with crisp rounded corners
		def group(state):
			w = h = 12
			p = Painter(w, h)
			outer = rounded_rows((0, 0, w, h), 5)
			inner = rounded_rows((1, 1, w - 2, h - 2), 4)
			p.spans(subtract_rows(outer, inner), mix(bg, BLACK, 0.3))
			return p
		self.stateful("group", None, (5, 5, 5, 5), group, ("normal",))

		# scroll views keep Haiku's frame
		def view_frame(state):
			p = Painter(8, 8)
			r = p.frame((0, 0, 8, 8), tint(bg, 1.0735), tint(bg, 1.0735),
				tint(bg, 0.59), tint(bg, 0.59))
			color = self.navigation if state == "focused" else tint(bg, 1.29)
			p.frame(r, *[color] * 4)
			return p
		self.stateful("common", None, (2, 2, 2, 2), view_frame,
			("normal", "focused"))

	# scroll bars: arrows straight on the track, a pill for a thumb

	def scroll_bars(self):
		size = self.scroll_width
		base = self.panel
		border = tint(base, 1.29)
		track = mix(base, WHITE, 0.3)

		def arrow_button(state, direction):
			# no button face, no divider: the groove's edge is the divider
			p = Painter(size, size)
			p.fill(0, 0, size, size, border)
			fill = mix(track, BLACK, 0.1) if state == "pressed" else track
			if direction in ("up", "left"):
				area, d = (1, 1, size - 2, size - 1), "up"
			else:
				area, d = (1, 0, size - 2, size - 1), "down"
			p.fill(*area, fill)
			outline_color = self.arrow_color
			if state == "pressed":
				outline_color = self.outline_pressed
			elif state == "disabled":
				outline_color = mix(self.arrow_color, track, 0.55)
			outline, inside = gonx_triangle(area, d)
			pixels(p, outline, outline_color)
			pixels(p, inside, WHITE)
			return p

		for direction in ("up", "down", "left", "right"):
			self.whole("sarrow-" + direction, lambda s, d=direction:
				arrow_button("normal" if s in ("focused", "toggled") else s, d))

		# the groove: a dark edge and a shade where it starts, a soft edge
		# where it ends
		def groove(state):
			p = Painter(size, 12)
			p.fill(0, 0, size, 12, border)
			p.fill(1, 0, size - 2, 12, track)
			p.fill(1, 0, size - 2, 1, mix(base, BLACK, 0.18))
			p.fill(1, 1, size - 2, 1, mix(track, BLACK, 0.04))
			p.fill(1, 11, size - 2, 1, mix(base, BLACK, 0.08))
			return p
		self.stateful("sgroove", "sgroove", (1, 2, 1, 1), groove, ("normal",))

		# DrawScrollBarThumb(): a pill, rounded across the bar
		radius = (size - 4) // 2
		cap = radius + 1

		def thumb(state):
			h = 2 * cap + 4
			p = Painter(size, h)
			p.fill(0, 0, size, h, border)
			p.fill(1, 0, size - 2, h, mix(self.thumb, WHITE, 0.3))
			if state == "disabled":
				return p
			pill = (2, 1, size - 4, h - 2)
			p.spans(rounded_rows(pill, radius), mix(self.thumb, BLACK, 0.45))
			p.spans(rounded_rows((3, 2, size - 6, h - 4), radius - 1),
				mix(self.thumb, WHITE, 0.6))
			return p
		self.stateful("sslider", "sslider", (0, cap, 0, cap), thumb,
			("normal", "focused", "pressed", "disabled"))
		for state in ("normal", "focused", "pressed", "disabled"):
			self.invisible("sgrip-" + state, 3, 3)

	# sliders: the pill groove with notches and the round knob

	def sliders(self):
		base = self.panel
		width = self.slider_width
		fill = mix(base, BLACK, 0.06)

		def bar(state):
			disabled = state == "disabled"
			color = mix(fill, base, 0.5) if disabled else fill
			outline = mix(self.outline, base, 0.5) if disabled else self.outline
			p = Painter(width, 16)
			p.fill(0, 4, width, 8, outline)
			p.fill(1, 4, width - 2, 8, color)
			c = width / 2
			for top, cy in ((True, 4), (False, 12)):
				for side in ("left", "right"):
					half_disc(p, c, cy, c, side, top, outline)
					half_disc(p, c, cy, c - 1, side, top, color)
			return p
		self.stateful("slider", "slider", (0, 4, 0, 4), bar,
			("normal", "toggled", "disabled"))

		# the notches, tiled along the groove
		p = Painter(width, 6)
		p.fill(2, 2, width - 4, 1, mix(fill, BLACK, 0.12))
		self.sheet.add("slider-normal-pattern", p)

		p = Painter(5, 1)
		p.fill(0, 0, 5, 1, tint(base, 1.29))
		self.sheet.add("slider-tick-normal", p)

		# DrawSliderThumb(): a round knob, white at the top
		across, along = self.slider_handle

		def knob(state):
			disabled = state == "disabled"
			p = Painter(across, along)
			w, h = across - 1, along - 1
			radius = (min(w, h) - 1) // 2
			if not disabled:
				self.round_rect(p, 1, 1, w, h, radius + 0.5, BLACK, 50 / 255)
			border = self.outline if not disabled else mix(self.outline, base,
				0.5)
			self.round_rect(p, 0, 0, w, h, radius + 0.5, border)
			self.round_rect(p, 1, 1, w - 2, h - 2, radius - 0.5,
				horizontal(1, 0, w - 2, [(0, WHITE),
					(1, mix(base, WHITE, 0.6 if disabled else 0.3))]))
			return p
		self.whole("slidercursor", lambda s: knob(
			s if s == "disabled" else "normal"))

	def round_rect(self, p, x, y, w, h, r, paint, opacity=1.0):
		r = min(r, w / 2, h / 2)
		cmd = [("M", x + r, y), ("L", x + w - r, y),
			("E", r, r, x + w, y + r), ("L", x + w, y + h - r),
			("E", r, r, x + w - r, y + h), ("L", x + r, y + h),
			("E", r, r, x, y + h - r), ("L", x, y + r),
			("E", r, r, x + r, y), ("Z",)]
		p.path(cmd, (math.floor(x), math.floor(y), math.ceil(w), math.ceil(h)),
			paint, opacity)

	# tabs: rounded at the top, the inactive ones lower and darker

	def tabs(self):
		base = self.panel
		line = mix(self.border, base, 0.25)
		radius = 5
		drop = self.tab_offset
		top_frame = drop + radius
		w = 2 * radius + 4
		h = top_frame + 8 + 1

		def tab(state):
			active = state in ("toggled", "pressed")
			p = Painter(w, h)
			y = 0 if active else drop
			if active:
				border = self.border
				top = mix(base, WHITE, 0.8)
				bottom = mix(base, WHITE, 0.45)
			else:
				border = line
				top = mix(base, BLACK, 0.02)
				bottom = mix(base, BLACK, 0.07)
				if state == "focused":
					top, bottom = mix(top, WHITE, 0.4), mix(bottom, WHITE, 0.4)
			gradient = vertical(0, y, h - y, [(0, top), (1, bottom)])
			rounded_top(p, (0, y, w, h - y), radius, border)
			rounded_top(p, (1, y + 1, w - 2, h - y - 1), radius - 1,
				gradient)
			return p
		self.stateful("tab", "tab", (radius, top_frame, radius, 1), tab,
			("normal", "focused", "toggled", "disabled"))

		# the tab view: one line along the content
		def tab_frame(state):
			p = Painter(6, 6)
			p.fill(0, 0, 6, 1, line)
			return p
		self.stateful("tabframe", None, (0, 1, 0, 0), tab_frame, ("normal",))

	# menus

	def menus(self):
		base = self.menu
		selected = self.menu_selected

		def menu(state):
			p = Painter(8, 8)
			r = p.frame((0, 0, 8, 8), *[mix(base, BLACK, 0.3)] * 4)
			p.fill(*r, base)
			return p
		self.stateful("menu", "menu", (1, 1, 1, 1), menu, ("normal",))

		# a flat bar in the selection colour with a slightly darker edge
		def item(state):
			p = Painter(8, 8)
			if state in ("focused", "pressed", "toggled"):
				r = p.frame((0, 0, 8, 8), *[mix(selected, BLACK, 0.15)] * 4)
				p.fill(*r, selected)
			return p
		self.stateful("menuitem", "menuitem", (1, 1, 1, 1), item,
			("normal", "focused", "pressed", "toggled"))
		self.stateful("menubaritem", "menubaritem", (1, 1, 1, 1), item,
			("normal", "focused", "pressed", "toggled"))

		# flat, with a hairline where it meets the window
		def menu_bar(state):
			p = Painter(8, 8)
			light = mix(base, WHITE, 0.6)
			r = p.frame((0, 0, 8, 8), light, light, base,
				mix(base, BLACK, 0.12))
			p.fill(*r, base)
			return p
		self.stateful("menubar", "menubar", (1, 1, 1, 1), menu_bar,
			("normal",))

		self.separator_lines("mitem-separator", base, 8, 6)

		for direction in ("left", "right"):
			def marker(state, direction=direction):
				p = Painter(9, 9)
				outline = self.arrow_color if state != "disabled" else mix(
					self.arrow_color, base, 0.6)
				o, i = gonx_triangle((0, 0, 9, 9), direction)
				pixels(p, o, outline)
				pixels(p, i, WHITE)
				return p
			self.whole("mitem-" + direction, marker, ("normal", "focused",
				"disabled"))
		self.invisible("mitem-tearoff-normal")
		self.invisible("mitem-tearoff-focused")

	def views(self):
		def item(state):
			p = Painter(6, 6)
			if state in ("pressed", "toggled"):
				p.fill(0, 0, 6, 6, self.list_selected)
			return p
		self.stateful("itemview", "itemview", (1, 1, 1, 1), item,
			("normal", "focused", "pressed", "toggled", "disabled"))

		# column headers: the button face, with lines between them
		bg = self.panel

		def header(state):
			p = Painter(8, 12)
			p.fill(0, 0, 8, 12, self.face_color(state))
			p.fill(0, 11, 8, 1, mix(self.outline, bg, 0.25))
			p.fill(7, 0, 1, 11, mix(self.outline, bg, 0.25))
			return p
		self.stateful("header", "header", (1, 1, 1, 1), header,
			("normal", "focused", "pressed", "disabled"))
		p = Painter(1, 8)
		p.fill(0, 0, 1, 8, mix(self.outline, bg, 0.25))
		self.sheet.add("header-separator", p)

	def arrows(self):
		base = self.panel

		# every arrow is the concept's outlined white triangle
		def arrow(state, direction, size=9):
			p = Painter(size, size)
			outline = self.arrow_color
			if state == "disabled":
				outline = mix(self.arrow_color, base, 0.6)
			elif state == "pressed":
				outline = self.outline_pressed
			elif state == "focused":
				outline = self.navigation
			o, i = gonx_triangle((0, 0, size, size), direction)
			pixels(p, o, outline)
			pixels(p, i, WHITE)
			return p
		for direction in ("up", "down", "left", "right"):
			self.whole("arrow-" + direction, lambda s, d=direction:
				arrow(s, d))
		self.whole("tree-plus", lambda s: arrow(s, "right"),
			("normal", "focused", "pressed", "disabled"))
		self.whole("tree-minus", lambda s: arrow(s, "down"),
			("normal", "focused", "pressed", "disabled"))


# the configuration: the same sections for both looks, their sizes and
# colours filled in

def kvconfig(look):
	# frame widths as the parts were cut: left, top, right, bottom
	w = look.widths
	n = dict(look.numbers,
		scroll=look.scroll_width, slider=look.slider_width,
		handle_across=look.slider_handle[0],
		handle_along=look.slider_handle[1], check=look.check_size,
		bf=w["button"][0], lt=w["lineedit"][1], lb=w["lineedit"][3],
		tabs=w["tab"][0], tabt=w["tab"][1], tft=w["tabframe"][1],
		tfs=w["tabframe"][0], ssl=w["sslider"][0], sst=w["sslider"][1],
		sgt=w["sgroove"][1], sgb=w["sgroove"][3], cft=w[look.combo][1],
		cfb=w[look.combo][3], gf=w["group"][0])
	c = look.colors
	general = """[%%General]
author=Hokku contributors
comment=%(comment)s
x11drag=menubar_and_primary_toolbar
alt_mnemonic=true
click_behavior=0
left_tabs=true
attach_active_tab=true
joined_inactive_tabs=false
embedded_tabs=false
mirror_doc_tabs=true
no_active_tab_separator=true
active_tab_overlap=0
bold_active_tab=false
group_toolbar_buttons=false
toolbar_item_spacing=1
toolbar_interior_spacing=2
toolbar_separator_thickness=4
center_toolbar_handle=true
slim_toolbars=false
toolbutton_style=0
spread_progressbar=true
progressbar_thickness=%(progress)d
spread_header=true
spread_menuitems=true
menubar_mouse_tracking=true
merge_menubar_with_toolbar=false
composite=false
menu_shadow_depth=0
tooltip_shadow_depth=0
submenu_overlap=0
menu_separator_height=6
scrollable_menu=true
translucent_windows=false
blurring=false
popup_blurring=false
animate_states=false
no_inactiveness=true
no_window_pattern=true
splitter_width=7
scroll_width=%(scroll)d
scroll_min_extent=20
center_scrollbar_indicator=true
scroll_arrows=true
scrollbar_in_view=false
transient_scrollbar=false
tree_branch_line=false
groupbox_top_label=false
slider_width=%(slider)d
slider_handle_width=%(handle_across)d
slider_handle_length=%(handle_along)d
check_size=%(check)d
tooltip_delay=-1
submenu_delay=250
layout_spacing=%(spacing)d
layout_margin=%(margin)d
small_icon_size=16
large_icon_size=32
button_icon_size=16
toolbar_icon_size=16
inline_spin_indicators=false
inline_spin_separator=false
vertical_spin_indicators=false
spin_button_width=%(spin)d
combo_as_lineedit=true
combo_menu=true
combo_focus_rect=false
square_combo_button=false
fill_rubberband=false
remove_extra_frames=false
dialog_button_layout=0
""" % dict(n, comment=look.comment)

	colors = """
[GeneralColors]
window.color=%(window)s
base.color=%(base)s
alt.base.color=%(alt_base)s
button.color=%(button)s
light.color=%(light)s
mid.light.color=%(mid_light)s
dark.color=%(dark)s
mid.color=%(mid)s
shadow.color=%(shadow)s
highlight.color=%(highlight)s
inactive.highlight.color=%(highlight)s
tooltip.base.color=%(tooltip)s
text.color=%(text)s
window.text.color=%(text)s
button.text.color=%(text)s
disabled.text.color=%(disabled_text)s
tooltip.text.color=%(text)s
highlight.text.color=%(highlight_text)s
link.color=%(link)s
link.visited.color=%(visited)s
progress.indicator.text.color=%(text)s

[Hacks]
transparent_ktitle_label=false
transparent_dolphin_view=false
transparent_pcmanfm_sidepane=false
transparent_pcmanfm_view=false
transparent_menutitle=true
respect_darkness=false
force_size_grip=false
tint_on_mouseover=0
no_selection_tint=true
disabled_icon_opacity=50
normal_default_pushbutton=true
iconless_pushbutton=false
transparent_arrow_button=true
iconless_menu=false
single_top_toolbar=false
style_vertical_toolbars=false
""" % c

	t = c["text"]
	sections = """
[PanelButtonCommand]
frame=true
frame.element=button
frame.top=%(bf)d
frame.bottom=%(bf)d
frame.left=%(bf)d
frame.right=%(bf)d
interior=true
interior.element=button
indicator.element=arrow
indicator.size=9
focusFrame=true
text.normal.color=%(t)s
text.focus.color=%(t)s
text.press.color=%(t)s
text.toggle.color=%(t)s
text.shadow=false
text.bold=false
text.margin.top=%(bmv)d
text.margin.bottom=%(bmv)d
text.margin.left=%(bmh)d
text.margin.right=%(bmh)d
text.iconspacing=4
min_width=%(bmin)s

[PanelButtonTool]
inherits=PanelButtonCommand
focusFrame=false
text.margin.left=%(tmh)d
text.margin.right=%(tmh)d
min_width=0

[ToolbarButton]
text.normal.color=%(t)s
text.focus.color=%(t)s
text.press.color=%(t)s
text.toggle.color=%(t)s

[Dock]
inherits=PanelButtonCommand
frame.element=dock
interior.element=dock
frame.top=1
frame.bottom=1
frame.left=1
frame.right=1
focusFrame=false
indicator.element=mdi

[DockTitle]
inherits=PanelButtonCommand
frame=false
interior=true
interior.element=docktitle
focusFrame=false
text.bold=true
text.margin.top=2
text.margin.bottom=2
text.margin.left=4
text.margin.right=4

[IndicatorSpinBox]
inherits=PanelButtonCommand
frame.element=button
interior.element=button
indicator.element=spin
indicator.size=%(spinglyph)d
focusFrame=false

[RadioButton]
inherits=PanelButtonCommand
frame=false
interior.element=radio
focusFrame=false
text.margin.top=1
text.margin.bottom=1
text.margin.left=4
text.margin.right=2
min_width=0

[CheckBox]
inherits=PanelButtonCommand
frame=false
interior.element=checkbox
focusFrame=false
text.margin.top=1
text.margin.bottom=1
text.margin.left=4
text.margin.right=2
min_width=0

[Focus]
frame=true
frame.element=focus
frame.top=1
frame.bottom=1
frame.left=1
frame.right=1
interior=false

[GenericFrame]
frame=true
frame.element=common
frame.top=2
frame.bottom=2
frame.left=2
frame.right=2
interior=false
text.normal.color=%(t)s

[LineEdit]
frame=true
frame.element=lineedit
interior=true
interior.element=lineedit
frame.top=%(lt)d
frame.left=%(lt)d
frame.bottom=%(lb)d
frame.right=%(lb)d
text.margin.top=%(lmv)d
text.margin.bottom=%(lmv)d
text.margin.left=3
text.margin.right=3
indicator.element=carrow
indicator.size=%(carrow)d

[DropDownButton]
inherits=PanelButtonCommand
indicator.element=arrow-down

[IndicatorArrow]
indicator.element=arrow
indicator.size=9

[ToolboxTab]
inherits=PanelButtonCommand
text.normal.color=%(t)s
text.focus.color=%(t)s
text.press.color=%(t)s

[Tab]
inherits=PanelButtonCommand
frame.element=tab
interior.element=tab
indicator.element=tab
indicator.size=12
focusFrame=false
frame.top=%(tabt)d
frame.bottom=1
frame.left=%(tabs)d
frame.right=%(tabs)d
text.margin.top=%(tabmt)d
text.margin.bottom=%(tabmb)d
text.margin.left=%(tabmh)d
text.margin.right=%(tabmh)d
min_width=0

[TabFrame]
frame=true
frame.element=tabframe
interior=false
frame.top=%(tft)d
frame.bottom=%(tfs)d
frame.left=%(tfs)d
frame.right=%(tfs)d

[TabBarFrame]
frame=false
interior=false

[TreeExpander]
indicator.element=tree
indicator.size=9

[HeaderSection]
inherits=PanelButtonCommand
frame.element=header
interior.element=header
frame.top=1
frame.bottom=1
frame.left=1
frame.right=1
indicator.element=arrow
indicator.size=8
focusFrame=false
text.margin.top=2
text.margin.bottom=2
text.margin.left=4
text.margin.right=4
min_width=0

[SizeGrip]
indicator.element=sizegrip

[Toolbar]
frame=false
interior=true
interior.element=toolbar
indicator.element=toolbar
indicator.size=6
text.normal.color=%(t)s
text.focus.color=%(t)s

[Slider]
frame=true
frame.element=slider
interior=true
interior.element=slider
frame.top=4
frame.bottom=4
frame.left=0
frame.right=0
focusFrame=false
%(pattern)s
[SliderCursor]
frame=false
interior=true
interior.element=slidercursor

[Progressbar]
frame=true
frame.element=progress
interior=true
interior.element=progress
frame.top=2
frame.bottom=2
frame.left=2
frame.right=2
text.margin=0
text.normal.color=%(t)s
text.focus.color=%(t)s
text.press.color=%(t)s
text.toggle.color=%(t)s
text.bold=false

[ProgressbarContents]
frame=true
frame.element=progress-pattern
interior=true
interior.element=progress-pattern
frame.top=2
frame.bottom=2
frame.left=2
frame.right=2

[ItemView]
frame=true
frame.element=itemview
interior=true
interior.element=itemview
frame.top=1
frame.bottom=1
frame.left=1
frame.right=1
text.margin.top=1
text.margin.bottom=1
text.margin.left=3
text.margin.right=3
text.normal.color=%(t)s
text.focus.color=%(t)s
text.press.color=%(hlt)s
text.toggle.color=%(hlt)s

[Splitter]
frame=false
interior=true
interior.element=splitter
indicator.element=splitter-grip
indicator.size=16

[Scrollbar]
indicator.element=sarrow
indicator.size=%(scroll)d

[ScrollbarSlider]
frame=true
frame.element=sslider
interior=true
interior.element=sslider
frame.top=%(sst)d
frame.bottom=%(sst)d
frame.left=%(ssl)d
frame.right=%(ssl)d
indicator.element=sgrip
indicator.size=3

[ScrollbarGroove]
frame=true
frame.element=sgroove
interior=true
interior.element=sgroove
frame.top=%(sgt)d
frame.bottom=%(sgb)d
frame.left=1
frame.right=1

[MenuItem]
frame=true
frame.element=menuitem
interior=true
interior.element=menuitem
indicator.element=mitem
indicator.size=9
frame.top=1
frame.bottom=1
frame.left=1
frame.right=1
text.margin.top=%(mimv)d
text.margin.bottom=%(mimv)d
text.margin.left=%(mimh)d
text.margin.right=%(mimh)d
min_height=0
text.normal.color=%(t)s
text.focus.color=%(t)s
text.press.color=%(t)s
text.toggle.color=%(t)s

[MenuBar]
frame=true
frame.element=menubar
interior=true
interior.element=menubar
frame.top=1
frame.bottom=1
frame.left=1
frame.right=1
text.normal.color=%(t)s

[MenuBarItem]
frame=true
frame.element=menubaritem
interior=true
interior.element=menubaritem
frame.top=1
frame.bottom=1
frame.left=1
frame.right=1
text.margin.top=%(mbmv)d
text.margin.bottom=%(mbmv)d
text.margin.left=%(mbmh)d
text.margin.right=%(mbmh)d
text.normal.color=%(t)s
text.focus.color=%(t)s
text.press.color=%(t)s
text.toggle.color=%(t)s

[TitleBar]
frame=false
interior=true
interior.element=titlebar
indicator.element=mdi
indicator.size=12
text.normal.color=#505050
text.focus.color=%(t)s
text.bold=true
text.margin.top=2
text.margin.bottom=2
text.margin.left=4
text.margin.right=4

[ComboBox]
inherits=PanelButtonCommand
frame.element=%(combo)s
interior.element=%(combo)s
indicator.element=carrow
indicator.size=%(carrow)d
frame.top=%(cft)d
frame.left=%(cft)d
frame.bottom=%(cfb)d
frame.right=%(cfb)d
text.margin.left=%(cmh)d
text.margin.right=2
min_width=0
min_height=%(cmin)s

[Menu]
frame=true
frame.element=menu
interior=true
interior.element=menu
frame.top=1
frame.bottom=1
frame.left=1
frame.right=1
text.normal.color=%(t)s

[GroupBox]
frame=true
frame.element=group
interior=false
frame.top=%(gf)d
frame.bottom=%(gf)d
frame.left=%(gf)d
frame.right=%(gf)d
text.margin=0
text.normal.color=%(t)s
text.focus.color=%(t)s
text.press.color=%(t)s
text.bold=true

[ToolTip]
frame=true
frame.element=tooltip
interior=true
interior.element=tooltip
frame.top=1
frame.bottom=1
frame.left=1
frame.right=1
text.margin.top=2
text.margin.bottom=2
text.margin.left=4
text.margin.right=4
text.normal.color=%(t)s

[StatusBar]
frame=false
interior=false

[Window]
interior=false
frame=false
""" % dict(n, t=t, hlt=c["highlight_text"])
	return general + colors + sections


class HaikuTheme(HaikuLook):
	comment = "Haiku's own control look, as Hokku draws it."
	check_size = 17
	scroll_width = 15
	slider_width = 7
	slider_handle = (14, 18)
	tab_offset = 4
	combo = "button"
	# text margins and the like, in pixels
	numbers = dict(progress=0, spacing=6, margin=8, spin=19,
		bmv=5, bmh=8, bmin="5font", tmh=4, spinglyph=9, lmv=2,
		tabmt=0, tabmb=2, tabmh=8, mimv=1, mimh=6, mbmv=2, mbmh=6,
		combo=combo, carrow=16, cmh=6, cmin="1.9font", pattern="")
	colors = dict(window="#d8d8d8", base="#ffffff", alt_base="#f4f4f4",
		button="#dedede", light="#ffffff", mid_light="#e7e7e7",
		dark="#989898", mid="#c8c8c8", shadow="#727272",
		highlight="#bebebe", tooltip="#ffffd8", text="#000000",
		disabled_text="#848484", highlight_text="#000000",
		link="#3366bb", visited="#91709b")


class GonxTheme(GonxLook):
	comment = "The Gonx BeOS concept look, as Hokku draws it."
	check_size = 13
	scroll_width = 15
	slider_width = 7
	slider_handle = (14, 18)
	tab_offset = 4
	combo = "combo"
	numbers = dict(progress=0, spacing=6, margin=8, spin=19,
		bmv=1, bmh=8, bmin="5font", tmh=3, spinglyph=9, lmv=2,
		tabmt=0, tabmb=2, tabmh=5, mimv=2, mimh=6, mbmv=2, mbmh=5,
		combo=combo, carrow=16, cmh=6, cmin="2.3font",
		# the groove's notches are its pattern, tiled every 6 pixels
		pattern="interior.x.patternsize=0\ninterior.y.patternsize=6\n")
	colors = dict(window="#ecebe6", base="#ffffff", alt_base="#f5f4f0",
		button="#f7f7f2", light="#ffffff", mid_light="#f7f7f2",
		dark="#94938d", mid="#c9c8c3", shadow="#6e6d67",
		highlight="#aabbd0", tooltip="#ffffd8", text="#000000",
		disabled_text="#9a9993", highlight_text="#000000",
		link="#3366bb", visited="#91709b")


# a check that whatever the configuration names is in the image, since
# Kvantum would otherwise fall back to its own, dark default theme

def parse(text):
	sections, current = {}, None
	for line in text.splitlines():
		line = line.strip()
		if line.startswith("["):
			current = sections.setdefault(line.strip("[]"), {})
		elif "=" in line and current is not None:
			key, value = line.split("=", 1)
			current[key] = value
	return sections


def check(config, ids):
	sections = parse(config)

	def get(name, key, default=None):
		seen = set()
		while name in sections and name not in seen:
			seen.add(name)
			if key in sections[name]:
				return sections[name][key]
			name = sections[name].get("inherits")
		return default

	missing = []

	def need(element):
		if element not in ids:
			missing.append(element)

	for name in sections:
		if name in ("%General", "GeneralColors", "Hacks", "ToolbarButton",
				"Focus"):
			continue
		if get(name, "frame") == "true":
			element = get(name, "frame.element")
			widths = {side: int(get(name, "frame." + side, "0"))
				for side in ("top", "bottom", "left", "right")}
			for piece in ("top", "bottom", "left", "right"):
				if widths[piece]:
					need("%s-normal-%s" % (element, piece))
			for v in ("top", "bottom"):
				for h in ("left", "right"):
					if widths[v] and widths[h]:
						need("%s-normal-%s%s" % (element, v, h))
		if get(name, "interior") == "true" and name not in ("CheckBox",
				"RadioButton"):
			need("%s-normal" % get(name, "interior.element"))

	indicator = lambda name: get(name, "indicator.element")
	for name in ("IndicatorArrow", "PanelButtonTool", "Scrollbar",
			"HeaderSection", "IndicatorSpinBox"):
		for d in ("up", "down"):
			for s in ("normal", "disabled"):
				need("%s-%s-%s" % (indicator(name), d, s))
	for d in ("left", "right"):
		need("%s-%s-normal" % (indicator("IndicatorArrow"), d))
		need("%s-%s-normal" % (indicator("MenuItem"), d))
	for d in ("plus", "minus"):
		need("%s-%s-normal" % (indicator("TreeExpander"), d))
		need("%s-%s-normal" % (indicator("IndicatorSpinBox"), d))
	for name in ("DropDownButton", "ComboBox", "ScrollbarSlider", "SizeGrip",
			"Splitter"):
		need("%s-normal" % indicator(name))
	need("%s-separator" % indicator("MenuItem"))
	need("%s-close-normal" % indicator("Tab"))
	need("%s-handle" % indicator("Toolbar"))
	need("%s-separator" % indicator("Toolbar"))
	need("%s-tick-normal" % get("Slider", "interior.element"))
	for name in ("CheckBox", "RadioButton"):
		element = get(name, "interior.element")
		need(element + "-normal")
		need(element + "-checked-normal")
		need(element + "-disabled")
		need(element + "-checked-disabled")
	need("checkbox-tristate-normal")
	for fixed in ("button-default-indicator", "header-separator", "dial",
			"dial-notches", "dial-handle", "focus-top", "focus-bottom",
			"focus-left", "focus-right"):
		need(fixed)
	if missing:
		raise SystemExit("missing elements: " + ", ".join(sorted(missing)))


def main():
	for theme in (GonxTheme(), HaikuTheme()):
		sheet = theme.build()
		config = kvconfig(theme)
		check(config, sheet.ids)
		folder = os.path.join(HERE, theme.name)
		os.makedirs(folder, exist_ok=True)
		with open(os.path.join(folder, theme.name + ".svg"), "w") as f:
			f.write(sheet.svg(theme.name))
		with open(os.path.join(folder, theme.name + ".kvconfig"), "w") as f:
			f.write(config)
		print("%s: %d elements" % (theme.name, len(sheet.ids)))


if __name__ == "__main__":
	main()
