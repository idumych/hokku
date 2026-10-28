/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 *
 * The Gonx window frame, after the "Gonx" BeOS concept artwork (Cotito,
 * c. 2001): a port of the Gonx app_server decorator for Renku OS and Haiku
 * (github.com/idumych/gonx) to cairo, for client-side decorations.
 *
 * A yellow tab whose left end turns down beside the window as a hatched
 * flap; close, zoom and minimize on a raised shelf right of the tab, which
 * slopes down into a thick blue-grey top band; a frame lit from the top
 * left, rounded at every corner.
 *
 * Shapes are filled row by row in whole pixels, as in the decorator, so
 * they match it to the pixel. Unlike app_server, a client-side frame can be
 * partly transparent: what lies outside the shapes is left clear, and the
 * flap's rounded corner and shadow blend with whatever is behind the window.
 *
 * The button glyphs come from Momo's IceWM theme "Gonx" (2004,
 * store.kde.org/p/1119093), copied pixel for pixel.
 */

#include "config.h"

#include <math.h>
#include <string.h>

#include <pango/pangocairo.h>

#include "cairo-util.h"
#include "gonx-frame.h"

struct color {
	double r, g, b;
};

static const struct color kFrame = { 158, 178, 202 };
static const struct color kInactiveFrame = { 166, 183, 204 };
static const struct color kOutline = { 79, 91, 107 };
static const struct color kFrameOutlineLight = { 95, 120, 150 };
static const struct color kFrameOutlineDark = { 14, 26, 42 };
static const struct color kFrameEdgeDark = { 92, 112, 136 };
static const struct color kFrameInnerDark = { 91, 102, 124 };
static const struct color kWhite = { 255, 255, 255 };
static const struct color kBlack = { 0, 0, 0 };
static const struct color kGlyphShadow = { 52, 59, 68 };

static const int kResizeKnobSize = 18;
static const int kBorderResizeLength = 22;


static int
imax(int a, int b)
{
	return a > b ? a : b;
}


static int
imin(int a, int b)
{
	return a < b ? a : b;
}


static double
clamp01(double v)
{
	return v < 0 ? 0 : v > 1 ? 1 : v;
}


static struct color
rgb(const uint8_t c[3])
{
	struct color result = { c[0], c[1], c[2] };
	return result;
}


static struct color
mix(struct color a, struct color b, double amount)
{
	struct color result;
	result.r = round(a.r + (b.r - a.r) * amount);
	result.g = round(a.g + (b.g - a.g) * amount);
	result.b = round(a.b + (b.b - a.b) * amount);
	return result;
}


static int
valid(const struct gonx_rect *r)
{
	return r->right >= r->left && r->bottom >= r->top;
}


static int
contains(const struct gonx_rect *r, int x, int y)
{
	return x >= r->left && x <= r->right && y >= r->top && y <= r->bottom;
}


static struct gonx_rect
make_rect(int left, int top, int right, int bottom)
{
	struct gonx_rect r = { left, top, right, bottom };
	return r;
}


static const struct gonx_rect kNoRect = { 0, 0, -1, -1 };


/* How far row `row` (0 = top) of a corner with radius `radius` is pushed in
 * from the straight edge. */
static int
corner_inset(double radius, int row)
{
	if (row >= radius)
		return 0;

	double distance = radius - row - 0.5;
	double inside = radius * radius - distance * distance;
	return (int)floor(radius - sqrt(inside > 0 ? inside : 0) + 0.5);
}


//	#pragma mark - drawing in whole pixels


static void
fill(cairo_t *cr, int left, int top, int right, int bottom, struct color c)
{
	if (right < left || bottom < top)
		return;

	cairo_set_source_rgb(cr, c.r / 255, c.g / 255, c.b / 255);
	cairo_rectangle(cr, left, top, right - left + 1, bottom - top + 1);
	cairo_fill(cr);
}


static void
pixel(cairo_t *cr, int x, int y, struct color c)
{
	fill(cr, x, y, x, y, c);
}


/* A pixel that replaces what is there, partly transparent. */
static void
pixel_alpha(cairo_t *cr, int x, int y, struct color c, double alpha)
{
	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_rgba(cr, c.r / 255, c.g / 255, c.b / 255, alpha);
	cairo_rectangle(cr, x, y, 1, 1);
	cairo_fill(cr);
	cairo_restore(cr);
}


static void
clear(cairo_t *cr, int left, int top, int right, int bottom)
{
	if (right < left || bottom < top)
		return;

	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_rectangle(cr, left, top, right - left + 1, bottom - top + 1);
	cairo_fill(cr);
	cairo_restore(cr);
}


//	#pragma mark - text


static PangoContext *
pango_context(void)
{
	static PangoContext *context;
	if (context == NULL) {
		PangoFontMap *fontmap = pango_cairo_font_map_get_default();
		context = pango_font_map_create_context(fontmap);
		/* sizes are in pixels, as on Haiku */
		pango_cairo_context_set_resolution(context, 72);
	}
	return context;
}


static const char *
title_font(const struct gonx_style *style, int small)
{
	const char *font = small ? style->small_title_font : style->title_font;
	return font[0] != '\0' ? font : "Sans Bold 11px";
}


static PangoLayout *
create_layout(const struct gonx_style *style, int small, const char *text)
{
	PangoLayout *layout = pango_layout_new(pango_context());
	PangoFontDescription *desc
		= pango_font_description_from_string(title_font(style, small));
	pango_layout_set_font_description(layout, desc);
	pango_font_description_free(desc);
	pango_layout_set_single_paragraph_mode(layout, TRUE);
	pango_layout_set_text(layout, text != NULL ? text : "", -1);
	return layout;
}


static void
font_metrics(const struct gonx_style *style, int small, int *ascent,
	     int *height)
{
	PangoFontDescription *desc
		= pango_font_description_from_string(title_font(style, small));
	PangoFontMetrics *metrics = pango_context_get_metrics(pango_context(),
		desc, NULL);
	double a = pango_font_metrics_get_ascent(metrics) / (double)PANGO_SCALE;
	double d = pango_font_metrics_get_descent(metrics) / (double)PANGO_SCALE;
	pango_font_metrics_unref(metrics);
	pango_font_description_free(desc);

	*ascent = (int)ceil(a);
	*height = (int)ceil(a + d);
}


static int
text_width(const struct gonx_style *style, int small, const char *text)
{
	if (text == NULL || text[0] == '\0')
		return 0;

	PangoLayout *layout = create_layout(style, small, text);
	PangoRectangle logical;
	pango_layout_get_pixel_extents(layout, NULL, &logical);
	g_object_unref(layout);
	return logical.width;
}


//	#pragma mark - geometry


static double
font_size(const struct gonx_style *style, int small)
{
	PangoFontDescription *desc
		= pango_font_description_from_string(title_font(style, small));
	double size = pango_font_description_get_size(desc) / (double)PANGO_SCALE;
	pango_font_description_free(desc);
	return size;
}


/* Sizes measured from the concept screenshots at a 12 pixel font, scaled
 * with the font like the stock decorators do. */
static void
geometry(struct gonx_layout *l, const struct gonx_style *style)
{
	const int small = l->small;
	double scale = font_size(style, small) / 12.0;
	if (scale < 1)
		scale = 1;
	l->scale = scale;

	font_metrics(style, small, &l->text_ascent, &l->text_height);

#define S(normal, smaller) ((int)round((small ? (smaller) : (normal)) * scale))
	l->side = S(5, 3);
	l->band = S(11, 7);
	/* the tab rises well above the band, as in Dano's */
	l->tab_height = imax(l->text_height + (int)round(3 * scale),
		l->band + S(9, 5));
	/* a strip of the band shows under the tab's foot, as in the concept */
	l->tab_gap = S(3, 2);
	/* the flap hangs entirely outside the window, beside its left border */
	l->flap_width = S(11, 7);
	l->flap_depth = S(11, 6);
	l->tab_radius = S(8, 5);
	l->tab_right_radius = S(10, 6);
	l->tab_foot_radius = S(8, 5);
	l->frame_radius = S(6, 4);
	l->title_padding = S(10, 7);
	l->button_size = S(10, 8);
	l->button_gap = (int)round(2 * scale);
	l->shelf_margin = S(5, 3);
	l->shelf_rise = S(4, 3);
	l->slope = S(8, 5);
#undef S
	l->text_offset = l->flap_width + (int)round(5 * scale);
	l->resize_length = (int)round(kBorderResizeLength * scale);
}


void
gonx_frame_sizes(const struct gonx_style *style, int small, int *top,
		 int *bottom, int *left, int *right)
{
	struct gonx_layout l;
	memset(&l, 0, sizeof l);
	l.small = small;
	geometry(&l, style);

	*top = l.tab_gap + l.tab_height;
	*bottom = l.side;
	*left = l.flap_width + l.side;
	*right = l.side;
}


void
gonx_frame_layout(struct gonx_layout *l, const struct gonx_style *style,
		  const char *title)
{
	int top, bottom, left, right;

	geometry(l, style);
	gonx_frame_sizes(style, l->small, &top, &bottom, &left, &right);

	l->content = make_rect(left, top, l->width - right - 1,
		l->height - bottom - 1);
	l->border = make_rect(l->content.left - l->side,
		l->content.top - l->band, l->content.right + l->side,
		l->content.bottom + l->side);

	if (l->border.right - l->border.left > 2 * kResizeKnobSize) {
		const int knob = (int)round(kResizeKnobSize * l->scale);
		l->resize = make_rect(l->border.right - knob, l->border.bottom - knob,
			l->border.right, l->border.bottom);
	} else
		l->resize = kNoRect;

	/* the tab, its shelf and buttons; the tab sits at the left edge */
	const int origin = 0;
	const int available = l->width;
	const int tab_bottom = l->content.top - l->tab_gap - 1;
	const int tab_top = tab_bottom - l->tab_height + 1;
	const int shelf_top = l->border.top - l->shelf_rise;
	const int min_title = l->text_offset + l->title_padding
		+ (int)round(16 * l->scale);

	int wanted = imax(min_title, text_width(style, l->small, title)
		+ l->text_offset + l->title_padding);

	int buttons = 0;
	for (int b = 0; b < GONX_BUTTON_COUNT; b++)
		buttons += l->has_button[b] ? 1 : 0;
	const int shelf = buttons == 0 ? 0
		: 2 * l->shelf_margin + buttons * l->button_size
			+ (buttons - 1) * l->button_gap + l->slope;

	if (wanted + shelf > available)
		wanted = imax(min_title, available - shelf);

	l->title = make_rect(origin, tab_top, origin + wanted - 1, tab_bottom);

	for (int b = 0; b < GONX_BUTTON_COUNT; b++)
		l->button[b] = kNoRect;

	if (shelf > 0) {
		l->shelf = make_rect(l->title.right + 1, shelf_top,
			l->title.right + shelf, l->border.top + 1);

		/* centred beside the tab, but clear of the shelf's top edge */
		const int size = l->button_size;
		const int button_top = imax(shelf_top + 2,
			(int)floor((shelf_top + tab_bottom + 1 - size) / 2.0));
		int x = l->shelf.left + l->shelf_margin;
		for (int b = 0; b < GONX_BUTTON_COUNT; b++) {
			if (!l->has_button[b])
				continue;
			l->button[b] = make_rect(x, button_top, x + size - 1,
				button_top + size - 1);
			x += size + l->button_gap;
		}
	} else
		l->shelf = kNoRect;

	/* the flap hangs beside the frame's left border without covering it */
	l->flap = make_rect(l->title.left, l->title.bottom + 1,
		l->border.left - 1, l->content.top + l->flap_depth);
}


//	#pragma mark - colours


static struct color
frame_color(int active)
{
	return active ? kFrame : kInactiveFrame;
}


static struct color
tab_color(const struct gonx_style *style, int active)
{
	return rgb(active ? style->tab : style->tab_inactive);
}


/* The tab's colour at row y: a blend from light at the tab's top to dark at
 * the bottom of the flap, so the flap carries on where the tab leaves off.
 */
static struct color
tab_color_at(const struct gonx_layout *l, const struct gonx_style *style,
	     int active, int y)
{
	const struct color base = tab_color(style, active);
	/* the concept's yellow stays bright down the tab and only darkens to
	 * gold towards the bottom of the flap */
	const struct color light = mix(base, kWhite, active ? 0.35 : 0.45);
	const struct color dark = mix(base, kBlack, active ? 0.16 : 0.1);

	const int top = l->title.top;
	const int bottom = l->content.top + l->flap_depth;
	const double position = bottom > top
		? clamp01((double)(y - top) / (bottom - top)) : 0;
	return mix(light, dark, position);
}


/* a dark shade of the tab's own colour, as in the concept's tabs */
static struct color
tab_outline(const struct gonx_style *style, int active)
{
	return active ? mix(rgb(style->tab), kBlack, 0.72)
		: mix(rgb(style->tab_inactive), kBlack, 0.38);
}


//	#pragma mark - the frame


/* Outlines the four rounded outer corners, light at the top left, dark at
 * the bottom right. */
static void
draw_corner_outlines(cairo_t *cr, const struct gonx_layout *l,
		     struct color highlight)
{
	const struct gonx_rect *r = &l->border;
	const double radius = l->frame_radius;
	const int rows = imin((int)radius, (r->bottom - r->top + 1) / 2);
	const double half = rows / 2.0;

	for (int row = 0; row < rows; row++) {
		const int inset = corner_inset(radius, row);
		const int previous = row == 0 ? inset + 1
			: corner_inset(radius, row - 1);
		const int end = imax(inset, previous - 1);

		for (int i = 0; i < 2; i++) {
			const int top = i == 0;
			const int y = top ? r->top + row : r->bottom - row;

			const struct color left_outline = top || row >= half
				? kFrameOutlineLight : kFrameOutlineDark;
			const struct color right_outline = top && row < half
				? kFrameOutlineLight : kFrameOutlineDark;
			const struct color left_inner = top || row >= half
				? highlight : kFrameEdgeDark;
			const struct color right_inner = top && row < half
				? highlight : kFrameEdgeDark;

			fill(cr, r->left + inset, y, r->left + end, y, left_outline);
			fill(cr, r->right - end, y, r->right - inset, y, right_outline);
			if (row > 0) {
				pixel(cr, r->left + end + 1, y, left_inner);
				pixel(cr, r->right - end - 1, y, right_inner);
			}
		}

		/* what the rounding cuts off is left clear */
		clear(cr, r->left, r->top + row, r->left + inset - 1, r->top + row);
		clear(cr, r->right - inset + 1, r->top + row, r->right, r->top + row);
		clear(cr, r->left, r->bottom - row, r->left + inset - 1,
			r->bottom - row);
		clear(cr, r->right - inset + 1, r->bottom - row, r->right,
			r->bottom - row);
	}
}


static const char *const kInnerCorner[] = {
	"BBBD",
	"BBD.",
	"BD..",
	"D..."
};


/* The content's corners are rounded too: the dark line round the content
 * follows the curve. */
static void
draw_inner_corners(cairo_t *cr, const struct gonx_layout *l, struct color base,
		   int knob)
{
	const struct gonx_rect line = make_rect(l->content.left - 1,
		l->content.top - 1, l->content.right + 1, l->content.bottom + 1);

	for (int corner = 0; corner < 4; corner++) {
		const int right = corner == 1 || corner == 3;
		const int bottom = corner >= 2;
		if (right && bottom && knob)
			continue;

		for (int y = 0; y < 4; y++) {
			for (int x = 0; x < 4; x++) {
				const char p = kInnerCorner[y][x];
				if (p == '.')
					continue;
				pixel(cr, right ? line.right - x : line.left + x,
					bottom ? line.bottom - y : line.top + y,
					p == 'D' ? kFrameInnerDark : base);
			}
		}
	}
}


/* A two-tone bracket inside the bottom right corner marks where the window
 * is resized, like Dano's frame. */
static void
draw_resize_grip(cairo_t *cr, const struct gonx_layout *l,
		 struct color highlight)
{
	if (l->side < 4)
		return;

	const int length = (int)round(12 * l->scale);
	const int right = l->border.right;
	const int bottom = l->border.bottom;
	const struct color dark = kFrameEdgeDark;

	fill(cr, right - 3, bottom - length, right - 3, bottom - 5, dark);
	fill(cr, right - 2, bottom - length, right - 2, bottom - 4, highlight);
	pixel(cr, right - 4, bottom - 4, dark);
	pixel(cr, right - 3, bottom - 3, highlight);
	fill(cr, right - length, bottom - 3, right - 5, bottom - 3, dark);
	fill(cr, right - length, bottom - 2, right - 4, bottom - 2, highlight);
}


static void
draw_line(cairo_t *cr, int x0, int y0, int x1, int y1, struct color c)
{
	/* diagonal or straight lines in whole pixels */
	const int steps = imax(abs(x1 - x0), abs(y1 - y0));
	for (int i = 0; i <= steps; i++) {
		const int x = steps == 0 ? x0 : x0 + (x1 - x0) * i / steps;
		const int y = steps == 0 ? y0 : y0 + (y1 - y0) * i / steps;
		pixel(cr, x, y, c);
	}
}


static void
draw_resize_knob(cairo_t *cr, const struct gonx_layout *l, struct color base,
		 int active)
{
	const struct gonx_rect knob = make_rect(l->resize.left, l->resize.top,
		l->content.right, l->content.bottom);
	if (!valid(&knob))
		return;

	fill(cr, knob.left, knob.top, knob.right, knob.bottom, base);
	const struct color edge = mix(base, kOutline, 0.7);
	fill(cr, knob.left, knob.top, knob.right, knob.top, edge);
	fill(cr, knob.left, knob.top, knob.left, knob.bottom, edge);

	if (!active)
		return;

	/* three ridges across the corner */
	const struct color light = mix(base, kWhite, 0.5);
	for (int i = 1; i <= 3; i++) {
		const int d = i * 4;
		draw_line(cr, knob.right - d, knob.bottom, knob.right,
			knob.bottom - d, kOutline);
		draw_line(cr, knob.right - d + 1, knob.bottom, knob.right,
			knob.bottom - d + 1, light);
	}
}


static void
draw_frame(cairo_t *cr, const struct gonx_layout *l, int active)
{
	const struct gonx_rect *r = &l->border;
	const struct gonx_rect *c = &l->content;
	const int border = l->side;

	const struct color base = frame_color(active);
	const struct color highlight = mix(base, kWhite, 0.4);
	const struct color shade = mix(base, kFrameInnerDark, 0.25);

	/* body first, then the lines over it */
	fill(cr, r->left, r->top, r->right, c->top - 1, base);
	fill(cr, r->left, c->top, c->left - 1, c->bottom, base);
	fill(cr, c->right + 1, c->top, r->right, c->bottom, base);
	fill(cr, r->left, c->bottom + 1, r->right, r->bottom, base);

	/* outer edges: light top and left, dark right and bottom */
	fill(cr, r->left, r->top, r->right, r->top, kFrameOutlineLight);
	fill(cr, r->left, r->top, r->left, r->bottom, kFrameOutlineLight);
	fill(cr, r->right, r->top + 1, r->right, r->bottom, kFrameOutlineDark);
	fill(cr, r->left + 1, r->bottom, r->right, r->bottom, kFrameOutlineDark);

	/* the lines just inside them */
	if (border > 2) {
		fill(cr, r->left + 1, r->top + 1, r->right - 2, r->top + 1, highlight);
		fill(cr, r->left + 1, r->top + 1, r->left + 1, r->bottom - 2,
			highlight);
	}
	if (border > 3) {
		fill(cr, r->right - 1, r->top + 1, r->right - 1, r->bottom - 1,
			kFrameEdgeDark);
		fill(cr, r->left + 1, r->bottom - 1, r->right - 1, r->bottom - 1,
			kFrameEdgeDark);
	}

	/* round the content: a shading line, then the dark line next to it */
	if (border > 3) {
		fill(cr, c->left - 2, c->top - 2, c->right + 2, c->top - 2, shade);
		fill(cr, c->left - 2, c->bottom + 2, c->right + 2, c->bottom + 2,
			shade);
	}
	fill(cr, c->left - 1, c->top - 1, c->right + 1, c->top - 1,
		kFrameInnerDark);
	fill(cr, c->left - 1, c->bottom + 1, c->right + 1, c->bottom + 1,
		kFrameInnerDark);
	fill(cr, c->left - 1, c->top - 1, c->left - 1, c->bottom + 1,
		kFrameInnerDark);
	fill(cr, c->right + 1, c->top - 1, c->right + 1, c->bottom + 1,
		kFrameInnerDark);

	const int knob = l->document && l->resizable;
	draw_inner_corners(cr, l, base, knob);
	draw_corner_outlines(cr, l, highlight);

	if (l->resizable) {
		if (l->document)
			draw_resize_knob(cr, l, base, active);
		else
			draw_resize_grip(cr, l, highlight);
	}
}


//	#pragma mark - the tab


/* Fills rect with a vertical blend from top to bottom, rounding the two top
 * corners and curving the bottom right into the band, and outlines its top
 * and sides. */
static void
fill_top_rounded(cairo_t *cr, struct gonx_rect rect, int left_radius,
		 int right_radius, struct color top, struct color bottom,
		 struct color outline, struct color highlight,
		 int bottom_right_radius)
{
	if (!valid(&rect))
		return;

	const int height = rect.bottom - rect.top + 1;
	int previous_left = 0;
	int previous_right = 0;

	for (int row = 0; row < height; row++) {
		const int y = rect.top + row;
		const int inset_left = corner_inset(left_radius, row);
		const int from_bottom = height - 1 - row;
		const int foot_inset = corner_inset(bottom_right_radius, from_bottom);
		const int inset_right = imax(corner_inset(right_radius, row),
			foot_inset);
		const int left = rect.left + inset_left;
		const int right = rect.right - inset_right;

		if (row == 0) {
			fill(cr, left, y, right, y, outline);
			previous_left = inset_left;
			previous_right = inset_right;
			continue;
		}

		const struct color color = mix(top, bottom,
			height > 1 ? (double)row / (height - 1) : 0);
		fill(cr, left, y, right, y, row == 1 ? highlight : color);

		/* outline runs that join this row's edge to the previous one */
		const int left_end = rect.left + imax(inset_left, previous_left - 1);
		int right_join = imax(inset_right, previous_right - 1);
		if (from_bottom < bottom_right_radius) {
			/* at the bottom right the curve heads inwards, so join to the
			 * row below instead */
			const int below = from_bottom == 0 ? foot_inset + 2
				: corner_inset(bottom_right_radius, from_bottom - 1);
			right_join = imax(right_join, below - 1);
		}
		const int right_start = rect.right - right_join;
		fill(cr, left, y, left_end, y, outline);
		fill(cr, right_start, y, right, y, outline);

		if (row > 1 && left_end + 1 < right_start)
			pixel(cr, left_end + 1, y, highlight);

		previous_left = inset_left;
		previous_right = inset_right;
	}
}


static void
draw_shelf(cairo_t *cr, const struct gonx_layout *l, struct color base,
	   struct color outline, struct color highlight)
{
	const struct gonx_rect *shelf = &l->shelf;
	const int rows = l->border.top - shelf->top;

	int previous_right = shelf->right;
	for (int row = 0; row < rows; row++) {
		const int y = shelf->top + row;
		const int right = shelf->right
			- (int)round((double)l->slope * (rows - 1 - row) / rows);

		if (row == 0) {
			fill(cr, shelf->left, y, right, y, outline);
		} else {
			fill(cr, shelf->left, y, right, y, row == 1 ? highlight : base);
			fill(cr, imin(previous_right + 1, right), y, right, y, outline);
		}
		previous_right = right;
	}

	/* merge into the band: cover its top outline and highlight rows under
	 * the shelf, so only the slope separates the two */
	for (int row = 0; row < 2; row++) {
		const int y = l->border.top + row;
		if (y > shelf->bottom)
			break;
		fill(cr, shelf->left, y, row == 0 ? shelf->right - 1 : shelf->right,
			y, base);
	}
}


/* Dano's grip: light diagonals every fourth pixel, each followed by a
 * slightly darker one, over the tab's colour. */
static void
draw_hatch(cairo_t *cr, const struct gonx_layout *l,
	   const struct gonx_style *style, int active, struct gonx_rect rect)
{
	if (!valid(&rect))
		return;

	for (int y = rect.top; y <= rect.bottom; y++) {
		const struct color color = tab_color_at(l, style, active, y);
		const struct color light = mix(color, kWhite, 0.4);
		const struct color dark = mix(color, kBlack, 0.06);
		for (int x = rect.left; x <= rect.right; x++) {
			const int phase = ((x + y) % 4 + 4) % 4;
			if (phase == 0)
				pixel(cr, x, y, light);
			else if (phase == 2)
				pixel(cr, x, y, dark);
		}
	}
}


/* How far row y of the flap is cut in from its left edge: a quarter circle
 * as wide as the flap, whose bottom edge meets the frame square. */
static int
flap_inset(const struct gonx_rect *flap, int y)
{
	const int radius = imin(flap->right - flap->left + 1,
		flap->bottom - flap->top + 1);
	return corner_inset(radius, flap->bottom - y);
}


/* Signed distance from the flap's edge (positive outside), for the part of
 * its outline that faces left and down. */
static double
flap_distance(const struct gonx_rect *flap, double x, double y)
{
	const double radius = imin(flap->right - flap->left + 1,
		flap->bottom - flap->top + 1);
	const double left = flap->left;
	const double bottom = flap->bottom + 1;
	const double cx = left + radius;
	const double cy = bottom - radius;

	if (x < cx && y > cy)
		return hypot(x - cx, y - cy) - radius;
	return fmax(left - x, y - bottom);
}


/* The flap's rounded bottom left corner, drawn smoothly: each pixel near the
 * curve mixes the outline, the flap's colour (darkening just inside the
 * edge, as in the concept) and what lies outside by how much of it each
 * covers. Outside, a soft shadow falls down and to the right, blending with
 * whatever is behind the window. */
static void
draw_flap_edge(cairo_t *cr, const struct gonx_layout *l,
	       const struct gonx_style *style, int active)
{
	const struct gonx_rect *flap = &l->flap;
	const double radius = imin(flap->right - flap->left + 1,
		flap->bottom - flap->top + 1);
	const double cy = flap->bottom + 1 - radius;

	const struct color outline = tab_outline(style, active);
	/* only a hint of a shadow */
	const double kShadowOffset = 1.0;
	const double kShadowReach = 2.0;
	const double kShadowStrength = 0.22;

	const int last_x = imin(flap->right + 4, l->border.left - 1);
	for (int py = (int)floor(cy) - 1; py <= flap->bottom + 4; py++) {
		if (py >= l->height)
			break;
		for (int px = flap->left - 1; px <= last_x; px++) {
			if (px < 0)
				continue;
			const double x = px + 0.5;
			const double y = py + 0.5;
			const double d = flap_distance(flap, x, y);

			const double outside = clamp01(d + 0.5);
			const double inside = clamp01(-d - 0.5);
			const double line = 1 - outside - inside;

			const double shadow_distance = fmax(0, flap_distance(flap,
				x - kShadowOffset, y - kShadowOffset));
			const double shadow = kShadowStrength
				* fmax(0, 1 - shadow_distance / kShadowReach);

			const int row_inside = py <= flap->bottom
				&& px >= flap->left + flap_inset(flap, py);
			if (outside >= 1 && shadow < 0.02 && !row_inside)
				continue;
			if (inside >= 1 && d < -2.5)
				continue;

			const struct color fill_color = mix(
				tab_color_at(l, style, active, py), kBlack,
				0.3 * clamp01(2.5 + d));

			/* the shadow is black at `shadow` opacity over what is behind */
			const double alpha = inside + line + shadow * outside;
			if (alpha <= 0) {
				pixel_alpha(cr, px, py, kBlack, 0);
				continue;
			}
			struct color color;
			color.r = (fill_color.r * inside + outline.r * line) / alpha;
			color.g = (fill_color.g * inside + outline.g * line) / alpha;
			color.b = (fill_color.b * inside + outline.b * line) / alpha;
			pixel_alpha(cr, px, py, color, alpha);
		}
	}
}


static void
draw_flap(cairo_t *cr, const struct gonx_layout *l,
	  const struct gonx_style *style, int active)
{
	const struct gonx_rect *flap = &l->flap;
	if (!valid(flap))
		return;

	const struct color outline = tab_outline(style, active);

	for (int y = flap->top; y <= flap->bottom; y++) {
		const int inset = flap_inset(flap, y);
		const int left = flap->left + inset;

		/* the bottom row closes the flap over the frame's border */
		if (y == flap->bottom) {
			fill(cr, left, y, flap->right, y, outline);
			continue;
		}

		const struct color color = tab_color_at(l, style, active, y);
		fill(cr, left, y, flap->right - 1, y, color);

		/* outline: the left edge and its cut, joining this row's edge to
		 * the next row's, and the side facing the content */
		const int below = flap_inset(flap, y + 1);
		fill(cr, left, y, flap->left + imax(inset, below - 1), y, outline);
		pixel(cr, flap->right, y, outline);
		if (inset == 0 && below == 0) {
			pixel(cr, flap->left + 1, y, mix(color, kWhite, 0.35));
		} else {
			/* round the curve the edge is heavier, as in the concept */
			pixel(cr, flap->left + imax(inset, below - 1) + 1, y,
				mix(color, kBlack, 0.3));
		}

		/* the grip follows the cut down */
		draw_hatch(cr, l, style, active, make_rect(
			flap->left + imax(inset, below - 1) + 2, y, flap->right - 2, y));
	}

	draw_flap_edge(cr, l, style, active);

	/* the content's top left corner is the frame's: its dark inner line
	 * curves round it between the tab's foot and the flap */
	const struct color base = frame_color(active);
	for (int y = 0; y < 4; y++) {
		for (int x = 0; x < 4; x++) {
			const char p = kInnerCorner[y][x];
			if (p == '.')
				continue;
			pixel(cr, l->content.left - 1 + x, l->content.top - 1 + y,
				p == 'D' ? kFrameInnerDark : base);
		}
	}
}


static void
draw_title(cairo_t *cr, const struct gonx_layout *l,
	   const struct gonx_style *style, const char *title, int active)
{
	if (title == NULL || title[0] == '\0')
		return;

	const struct gonx_rect *t = &l->title;
	const int available = t->right - t->left + 1 - l->text_offset
		- l->title_padding;
	if (available <= 0)
		return;

	PangoLayout *layout = create_layout(style, l->small, title);
	pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_MIDDLE);
	pango_layout_set_width(layout, available * PANGO_SCALE);

	const int descent = l->text_height - l->text_ascent;
	const int baseline = (int)floor(((t->top + 2.0) + t->bottom
		+ l->text_height) / 2.0 - descent + 0.5);
	const int layout_baseline = pango_layout_get_baseline(layout) / PANGO_SCALE;

	const struct color c = rgb(active ? style->text : style->text_inactive);
	cairo_save(cr);
	cairo_set_source_rgb(cr, c.r / 255, c.g / 255, c.b / 255);
	cairo_move_to(cr, t->left + l->text_offset, baseline - layout_baseline);
	pango_cairo_update_context(cr, pango_layout_get_context(layout));
	pango_layout_context_changed(layout);
	pango_cairo_show_layout(cr, layout);
	cairo_restore(cr);

	g_object_unref(layout);
}


//	#pragma mark - buttons


/* 'W' white, 'S' its shadow, 'B' the pressed glyph, which drops one pixel
 * down and right and loses its shadow. */
static const char *const kGlyphs[GONX_BUTTON_COUNT][2][10] = {
	{
		{
			"WWW...WWW.",
			"WWWW.WWWWS",
			"WWWWWWWWWS",
			".WWWWWWWSS",
			"..WWWWWSS.",
			".WWWWWWW..",
			"WWWWWWWWW.",
			"WWWWSWWWWS",
			"WWWSS.WWWS",
			".SSS...SSS"
		}, {
			"BBB...BBB.",
			"BBBB.BBBB.",
			"BBBBBBBBB.",
			".BBBBBBB..",
			"..BBBBB...",
			".BBBBBBB..",
			"BBBBBBBBB.",
			"BBBB.BBBB.",
			"BBB...BBB.",
			".........."
		}
	}, {
		{
			"WWWW.WWWW.",
			"WWSSS.SWWS",
			"WSS.....WS",
			"WS......WS",
			".S..W....S",
			"W....S..W.",
			"WS......WS",
			"WW.....WWS",
			"WWWW.WWWWS",
			".SSSS.SSSS"
		}, {
			"..........",
			".BBBB.BBBB",
			".BB.....BB",
			".B.......B",
			".B.......B",
			".....B....",
			".B.......B",
			".B.......B",
			".BB.....BB",
			".BBBB.BBBB"
		}
	}, {
		{
			"WWWW......",
			"WWWWS.....",
			"WWWWS.....",
			"WWWWSWWWW.",
			".SSSSWWWWS",
			"...WWWWWWS",
			"...WWWWWWS",
			"...WWWWWWS",
			"...WWWWWWS",
			"....SSSSSS"
		}, {
			"..........",
			".BBBB.....",
			".BBBB.....",
			".BBBB.....",
			".BBBBBBBBB",
			"....BBBBBB",
			"....BBBBBB",
			"....BBBBBB",
			"....BBBBBB",
			"....BBBBBB"
		}
	}
};


/* The same glyphs drawn 8 x 8 for the smaller buttons of floating windows:
 * 7 x 7 shapes with the same one pixel shadow and pressed drop. */
static const char *const kSmallGlyphs[GONX_BUTTON_COUNT][2][8] = {
	{
		{
			"WW...WW.",
			"WWW.WWWS",
			".WWWWWSS",
			"..WWWSS.",
			".WWWWW..",
			"WWWSWWW.",
			"WWSS.WWS",
			".SS...SS"
		}, {
			"........",
			".BB...BB",
			".BBB.BBB",
			"..BBBBB.",
			"...BBB..",
			"..BBBBB.",
			".BBB.BBB",
			".BB...BB"
		}
	}, {
		{
			"WWW.WWW.",
			"WWSS.WWS",
			"WSS...WS",
			".S.W...S",
			"W...S.W.",
			"WW...WWS",
			"WWW.WWWS",
			".SSS.SSS"
		}, {
			"........",
			".BBB.BBB",
			".BB...BB",
			".B.....B",
			"....B...",
			".B.....B",
			".BB...BB",
			".BBB.BBB"
		}
	}, {
		{
			"WWW.....",
			"WWWS....",
			"WWWSWWW.",
			".SSSWWWS",
			"..WWWWWS",
			"..WWWWWS",
			"..WWWWWS",
			"...SSSSS"
		}, {
			"........",
			".BBB....",
			".BBB....",
			".BBB.BBB",
			".....BBB",
			"...BBBBB",
			"...BBBBB",
			"...BBBBB"
		}
	}
};


/* A glyph of `count` x `count` pixels, scaled up by whole pixels for large
 * fonts, centred in its rectangle on the frame colour. */
static void
draw_glyph(cairo_t *cr, struct gonx_rect rect, const char *const *rows,
	   int count, struct color background)
{
	if (!valid(&rect))
		return;

	fill(cr, rect.left, rect.top, rect.right, rect.bottom, background);

	const int size = imax(1, (rect.right - rect.left + 1) / count);
	const int ox = rect.left + (rect.right - rect.left + 1 - count * size) / 2;
	const int oy = rect.top + (rect.bottom - rect.top + 1 - count * size) / 2;

	for (int y = 0; y < count; y++) {
		for (int x = 0; x < count; x++) {
			struct color c;
			switch (rows[y][x]) {
				case 'W':
					c = kWhite;
					break;
				case 'S':
					c = kGlyphShadow;
					break;
				case 'B':
					c = kBlack;
					break;
				default:
					continue;
			}
			const int left = ox + x * size;
			const int top = oy + y * size;
			fill(cr, left, top, left + size - 1, top + size - 1, c);
		}
	}
}


//	#pragma mark - public


void
gonx_frame_paint(cairo_t *cr, const struct gonx_layout *l,
		 const struct gonx_style *style, const char *title, int active,
		 const int pressed[GONX_BUTTON_COUNT])
{
	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_rgba(cr, 0, 0, 0, 0);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);

	draw_frame(cr, l, active);

	const struct color base = frame_color(active);
	if (valid(&l->shelf))
		draw_shelf(cr, l, base, kOutline, mix(base, kWhite, 0.35));

	const struct color tab = tab_color(style, active);
	const struct color outline = tab_outline(style, active);
	const struct gonx_rect *t = &l->title;
	/* the left end is Dano's; the right end is the concept's, rounded at the
	 * top and curving into the band at the bottom */
	fill_top_rounded(cr, *t, l->tab_radius, l->tab_right_radius,
		tab_color_at(l, style, active, t->top),
		tab_color_at(l, style, active, t->bottom), outline,
		mix(tab, kWhite, 0.6), l->tab_foot_radius);

	/* the tab's foot; at the left it carries on down as the flap */
	fill(cr, valid(&l->flap) ? l->flap.right + 1 : t->left, t->bottom,
		t->right - corner_inset(l->tab_foot_radius, 0) - 1, t->bottom,
		outline);

	draw_hatch(cr, l, style, active, make_rect(t->left + 2, t->top + 3,
		t->left + l->flap_width - 2, t->bottom - 1));

	draw_flap(cr, l, style, active);

	cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
	draw_title(cr, l, style, title, active);
	cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);

	/* floating windows' buttons are too small for the 10 x 10 glyphs */
	const int small = l->button_size < 10;
	for (int b = 0; b < GONX_BUTTON_COUNT; b++) {
		const int state = pressed[b] ? 1 : 0;
		draw_glyph(cr, l->button[b],
			small ? kSmallGlyphs[b][state] : kGlyphs[b][state],
			small ? 8 : 10, base);
	}

	cairo_restore(cr);
}


void
gonx_frame_paint_overlay(cairo_t *cr, const struct gonx_layout *l, int active)
{
	const struct color base = frame_color(active);
	const int knob = l->document && l->resizable;

	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
	draw_inner_corners(cr, l, base, knob);
	if (knob)
		draw_resize_knob(cr, l, base, active);
	cairo_restore(cr);
}


int
gonx_frame_location(const struct gonx_layout *l, int x, int y)
{
	if (contains(&l->content, x, y))
		return THEME_LOCATION_CLIENT_AREA;

	if (contains(&l->title, x, y) || contains(&l->flap, x, y)
		|| (contains(&l->shelf, x, y) && y < l->border.top))
		return THEME_LOCATION_TITLEBAR;

	if (!contains(&l->border, x, y))
		return THEME_LOCATION_EXTERIOR;

	if (!l->resizable)
		return THEME_LOCATION_TITLEBAR;

	const struct gonx_rect corner = make_rect(
		l->border.right - l->resize_length,
		l->border.bottom - l->resize_length, l->border.right,
		l->border.bottom);
	if (contains(&corner, x, y)
		|| (l->document && contains(&l->resize, x, y)))
		return THEME_LOCATION_RESIZING_BOTTOM_RIGHT;

	if (x < l->content.left)
		return THEME_LOCATION_RESIZING_LEFT;
	if (x > l->content.right)
		return THEME_LOCATION_RESIZING_RIGHT;
	if (y > l->content.bottom)
		return THEME_LOCATION_RESIZING_BOTTOM;
	return THEME_LOCATION_RESIZING_TOP;
}


void
gonx_frame_input_rects(const struct gonx_layout *l,
		       void (*add)(void *data, int x, int y, int width,
				   int height),
		       void *data)
{
	const struct gonx_rect *rects[] = { &l->border, &l->title, &l->shelf,
		&l->flap };
	for (unsigned i = 0; i < sizeof rects / sizeof rects[0]; i++) {
		const struct gonx_rect *r = rects[i];
		if (valid(r))
			add(data, r->left, r->top, r->right - r->left + 1,
				r->bottom - r->top + 1);
	}
}
