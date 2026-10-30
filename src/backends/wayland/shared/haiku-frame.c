/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Haiku's window frame: a port of app_server's Default decorator
 * (TabDecorator's layout, DefaultDecorator's drawing) to cairo, for
 * client-side decorations.
 *
 * A tab as wide as the title needs at the window's top left, with the close
 * button at its left end and zoom at its right, filled with a blend of the
 * tab colour; round the content a bevelled frame in the window border
 * colour, light at the top left and dark at the bottom right. Document
 * windows have a resize knob over the content's corner, other resizable
 * windows two notches in the bottom right border.
 *
 * Lines and colours follow the decorator to the pixel. There is no window
 * menu button or minimize button, as on Haiku; what lies right of the tab is
 * left clear.
 */

#include "config.h"

#include <math.h>
#include <string.h>

#include <pango/pangocairo.h>

#include "cairo-util.h"
#include "haiku-frame.h"

struct color {
	double r, g, b;
};

/* the tints of InterfaceDefs.h */
static const float kLightenMaxTint = 0.0f;
static const float kLighten2Tint = 0.385f;
static const float kNoTint = 1.0f;
static const float kDarken1Tint = 1.147f;
static const float kDarken2Tint = 1.295f;
static const float kDarken3Tint = 1.407f;

static const float kResizeKnobSize = 18.0f;
static const float kBorderResizeLength = 22.0f;

static const struct color kWhite = { 255, 255, 255 };

static const struct gonx_rect kNoRect = { 0, 0, -1, -1 };


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


static struct color
rgb(const uint8_t c[3])
{
	struct color result = { c[0], c[1], c[2] };
	return result;
}


/* tint_color() from the Interface Kit */
static struct color
tint(struct color c, float tint)
{
	struct color result;
	if (tint < 1.0f) {
		result.r = (uint8_t)(255.0f - (255.0f - c.r) * tint);
		result.g = (uint8_t)(255.0f - (255.0f - c.g) * tint);
		result.b = (uint8_t)(255.0f - (255.0f - c.b) * tint);
	} else {
		result.r = (uint8_t)(c.r * (2 - tint));
		result.g = (uint8_t)(c.g * (2 - tint));
		result.b = (uint8_t)(c.b * (2 - tint));
	}
	return result;
}


static int
valid(const struct gonx_rect *r)
{
	return r->right >= r->left && r->bottom >= r->top;
}


static struct gonx_rect
make_rect(int left, int top, int right, int bottom)
{
	struct gonx_rect r = { left, top, right, bottom };
	return r;
}


static void
set_source(cairo_t *cr, struct color c)
{
	cairo_set_source_rgb(cr, c.r / 255, c.g / 255, c.b / 255);
}


static void
fill(cairo_t *cr, int left, int top, int right, int bottom, struct color c)
{
	if (right < left || bottom < top)
		return;

	set_source(cr, c);
	cairo_rectangle(cr, left, top, right - left + 1, bottom - top + 1);
	cairo_fill(cr);
}


/* StrokeLine() for the horizontal and vertical lines the decorator draws */
static void
line(cairo_t *cr, int x0, int y0, int x1, int y1, struct color c)
{
	fill(cr, imin(x0, x1), imin(y0, y1), imax(x0, x1), imax(y0, y1), c);
}


/* A linear gradient from `start` at (x0, y0) to `end` at (x1, y1) over
 * the rectangle. */
static void
fill_gradient(cairo_t *cr, struct gonx_rect r, double x0, double y0,
	      double x1, double y1, struct color start, struct color end)
{
	if (!valid(&r))
		return;

	cairo_pattern_t *pattern = cairo_pattern_create_linear(x0 + 0.5,
		y0 + 0.5, x1 + 0.5, y1 + 0.5);
	cairo_pattern_add_color_stop_rgb(pattern, 0, start.r / 255,
		start.g / 255, start.b / 255);
	cairo_pattern_add_color_stop_rgb(pattern, 1, end.r / 255, end.g / 255,
		end.b / 255);
	cairo_set_source(cr, pattern);
	cairo_rectangle(cr, r.left, r.top, r.right - r.left + 1,
		r.bottom - r.top + 1);
	cairo_fill(cr);
	cairo_pattern_destroy(pattern);
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


/* bold on titled windows, plain on floating ones, as the decorator does */
static PangoFontDescription *
title_font(const struct gonx_style *style, int small)
{
	const char *font = small ? style->small_title_font : style->title_font;
	return pango_font_description_from_string(font[0] != '\0' ? font
		: small ? "Sans 12px" : "Sans Bold 12px");
}


static double
font_size(const struct gonx_style *style, int small)
{
	PangoFontDescription *desc = title_font(style, small);
	double size = pango_font_description_get_size(desc) / (double)PANGO_SCALE;
	pango_font_description_free(desc);
	return size;
}


static void
font_height(const struct gonx_style *style, int small, double *ascent,
	    double *descent)
{
	PangoFontDescription *desc = title_font(style, small);
	PangoFontMetrics *metrics = pango_context_get_metrics(pango_context(),
		desc, NULL);
	*ascent = pango_font_metrics_get_ascent(metrics) / (double)PANGO_SCALE;
	*descent = pango_font_metrics_get_descent(metrics) / (double)PANGO_SCALE;
	pango_font_metrics_unref(metrics);
	pango_font_description_free(desc);
}


static PangoLayout *
create_layout(const struct gonx_style *style, int small, const char *text)
{
	PangoLayout *layout = pango_layout_new(pango_context());
	PangoFontDescription *desc = title_font(style, small);
	pango_layout_set_font_description(layout, desc);
	pango_font_description_free(desc);
	pango_layout_set_single_paragraph_mode(layout, TRUE);
	pango_layout_set_text(layout, text != NULL ? text : "", -1);
	return layout;
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
scale_factor(const struct gonx_style *style, int small)
{
	double scale = font_size(style, small) / 12.0;
	return scale < 1 ? 1 : scale;
}


static int
border_width(const struct gonx_style *style, int small)
{
	return (int)((small ? 3 : 5) * scale_factor(style, small));
}


/* Rows from the tab's top to the top of the border, which the tab's last
 * row covers. A floating window's tab is 4 pixels lower. */
static int
tab_rise(const struct gonx_style *style, int small)
{
	double ascent, descent;
	font_height(style, small, &ascent, &descent);
	const int rise = (int)ceil(ascent + descent
		+ border_width(style, small) * 1.4f);
	return small ? rise - 4 : rise;
}


void
haiku_frame_sizes(const struct gonx_style *style, int small, int tabless,
		  int *top, int *bottom, int *left, int *right)
{
	const int border = border_width(style, small);
	*bottom = *left = *right = border;
	*top = tabless ? border : border + tab_rise(style, small);
}


void
haiku_frame_layout(struct gonx_layout *l, const struct gonx_style *style,
		   const char *title)
{
	int top, bottom, left, right;
	haiku_frame_sizes(style, l->small, l->tabless, &top, &bottom, &left,
		&right);

	l->haiku = 1;
	l->scale = scale_factor(style, l->small);
	l->side = left;
	l->content = make_rect(left, top, l->width - right - 1,
		l->height - bottom - 1);
	l->border = make_rect(0, l->content.top - l->side, l->width - 1,
		l->height - 1);
	l->shelf = l->flap = kNoRect;
	for (int b = 0; b < GONX_BUTTON_COUNT; b++)
		l->button[b] = kNoRect;

	l->resize_length = (int)(kBorderResizeLength * l->scale);
	if (l->side > 1) {
		const int knob = (int)(kResizeKnobSize * l->scale);
		l->resize = make_rect(l->border.right - knob, l->border.bottom - knob,
			l->border.right, l->border.bottom);
	} else
		l->resize = kNoRect;

	if (l->tabless) {
		l->title = kNoRect;
		return;
	}

	/* TabDecorator::_GetButtonSizeAndOffset() and _DoTabLayout() */
	const int tab_bottom = l->border.top;
	const int tab_top = tab_bottom - tab_rise(style, l->small);
	const double size = font_size(style, l->small);
	const int offset = (int)floor(size / (l->small ? 2.6 : 2.3));
	const int inset = (int)floor(size / (l->small ? 5.0 : 6.0));
	const int button = tab_bottom - tab_top - 2 * offset + inset;
	const int has_close = l->has_button[GONX_CLOSE];
	const int has_zoom = l->has_button[GONX_ZOOM];

	l->button_size = button;
	l->text_offset = (int)(l->side * (l->small ? 3.4f : 3.6f));

	int min_tab = inset * 2 + l->text_offset;
	if (has_close)
		min_tab += offset + button;
	if (has_zoom)
		min_tab += offset + button;
	const int title_width = text_width(style, l->small, title);
	int max_tab = min_tab;
	if (title_width > 0)
		max_tab += title_width + l->text_offset;

	int tab_size = l->border.right - l->border.left;
	if (tab_size < min_tab)
		tab_size = min_tab;
	if (tab_size > max_tab)
		tab_size = max_tab;

	l->title = make_rect(l->border.left, tab_top,
		imin(l->border.left + tab_size, l->width - 1), tab_bottom);

	/* _LayoutTabItems(): the button rects are a pixel wider than the
	 * bitmaps drawn at their top left */
	const struct gonx_rect *t = &l->title;
	int close_right = t->left;
	int zoom_left = t->right;
	if (has_close) {
		l->button[GONX_CLOSE] = make_rect(t->left + offset, t->top + offset,
			t->left + offset + button - 1, t->top + offset + button - 1);
		close_right = t->left + offset + button;
	}
	if (has_zoom) {
		l->button[GONX_ZOOM] = make_rect(t->right - offset - button,
			t->top + offset, t->right - offset - 1,
			t->top + offset + button - 1);
		zoom_left = t->right - offset - button;
	}

	l->title_space = zoom_left - close_right - l->text_offset * 2 + inset;
	l->text_offset += close_right - t->left;
}


//	#pragma mark - colours


/* DefaultDecorator::GetComponentColors() for the borders */
static void
frame_colors(const struct gonx_style *style, int active, struct color c[6])
{
	const struct color base = rgb(active ? style->frame : style->frame_inactive);
	c[0] = tint(base, kDarken2Tint);
	c[1] = tint(base, kLighten2Tint);
	c[2] = base;
	c[3] = tint(base, (kDarken1Tint + kNoTint) / 2);
	c[4] = tint(base, kDarken2Tint);
	c[5] = tint(base, kDarken3Tint);
}


static struct color
tab_color(const struct gonx_style *style, int active)
{
	return rgb(active ? style->tab : style->tab_inactive);
}


static struct color
tab_light(const struct gonx_style *style, int active)
{
	return tint(tab_color(style, active),
		(kLightenMaxTint + kLighten2Tint) / 2);
}


//	#pragma mark - drawing


/* Which of frame_colors() line i of the border takes, counting in from
 * the outside: floating windows use three shades, the others five. */
static int
shade_index(const struct gonx_layout *l, int i)
{
	return (int)((float)i / (float)l->side * (l->small ? 3 : 5));
}


static int
light_shade(const struct gonx_layout *l, int i)
{
	const int index = shade_index(l, i);
	return l->small ? index * 2 : index;
}


static int
dark_shade(const struct gonx_layout *l, int i)
{
	const int index = shade_index(l, i);
	if (l->small)
		return 2 - index == 2 ? 5 : (2 - index) * 2;
	return 4 - index == 4 ? 5 : 4 - index;
}


static void
draw_frame(cairo_t *cr, const struct gonx_layout *l,
	   const struct gonx_style *style, int active)
{
	const struct gonx_rect *b = &l->border;
	const int width = l->side;
	struct color c[6];
	frame_colors(style, active, c);

	/* top, left, bottom, right, each overdrawing the corners of the one
	 * before: light shades at the top left, dark at the bottom right */
	for (int i = 0; i < width; i++) {
		line(cr, b->left + i, b->top + i, b->right - i, b->top + i,
			c[light_shade(l, i)]);
	}
	if (valid(&l->title)) {
		/* grey along the bottom of the tab, over the white */
		const int overdraw = (int)ceilf(width / 5.0f);
		for (int k = 1; k <= overdraw; k++) {
			line(cr, l->title.left + 2, l->title.bottom + k,
				l->title.right - 2, l->title.bottom + k, c[2]);
		}
	}
	for (int i = 0; i < width; i++) {
		line(cr, b->left + i, b->top + i, b->left + i, b->bottom - i,
			c[light_shade(l, i)]);
	}
	for (int i = 0; i < width; i++) {
		line(cr, b->left + i, b->bottom - i, b->right - i, b->bottom - i,
			c[dark_shade(l, i)]);
	}
	for (int i = 0; i < width; i++) {
		line(cr, b->right - i, b->top + i, b->right - i, b->bottom - i,
			c[dark_shade(l, i)]);
	}

	if (!l->resizable || l->document)
		return;

	/* the resize corner's two notches */
	line(cr, l->content.right + 1, b->bottom - l->resize_length,
		b->right - 1, b->bottom - l->resize_length, c[0]);
	line(cr, b->right - l->resize_length, l->content.bottom + 1,
		b->right - l->resize_length, b->bottom - 1, c[0]);
}


/* DefaultDecorator::_DrawResizeKnob(): over the content's corner, so it is
 * drawn again after the content */
static void
draw_resize_knob(cairo_t *cr, const struct gonx_layout *l,
		 const struct gonx_style *style, int active)
{
	if (!l->document || !l->resizable || !valid(&l->resize))
		return;

	struct color c[6];
	frame_colors(style, active, c);

	const int x = l->resize.right - 3;
	const int y = l->resize.bottom - 3;
	const int left = l->resize.left;
	const int top = l->resize.top;

	fill_gradient(cr, make_rect(left, top, x, y), left, top, x, y, c[1], c[2]);
	line(cr, left, top, left, y - 2, c[0]);
	line(cr, left + 1, top + 1, left + 1, y - 1, c[1]);
	line(cr, left, top, x - 2, top, c[0]);
	line(cr, left + 1, top + 1, x - 1, top + 1, c[1]);

	if (!active)
		return;

	for (int i = 1; i <= 4; i++) {
		for (int j = 1; j <= i; j++) {
			const int px = x - 3 * j + 1;
			const int py = y - 3 * (5 - i) + 1;
			fill(cr, px, py, px, py, c[0]);
			fill(cr, px + 1, py + 1, px + 1, py + 1, kWhite);
		}
	}
}


static void
draw_tab(cairo_t *cr, const struct gonx_layout *l,
	 const struct gonx_style *style, int active)
{
	const struct gonx_rect *t = &l->title;
	struct color frame[6];
	frame_colors(style, active, frame);
	const struct color tab = tab_color(style, active);

	/* outer frame, bevel and shadow */
	line(cr, t->left, t->top, t->left, t->bottom, frame[0]);
	line(cr, t->left, t->top, t->right, t->top, frame[0]);
	line(cr, t->right, t->top, t->right, t->bottom, frame[5]);
	line(cr, t->left + 1, t->top + 1, t->left + 1, t->bottom,
		tint(tab, kLighten2Tint));
	line(cr, t->left + 1, t->top + 1, t->right - 1, t->top + 1,
		tint(tab, kLighten2Tint));
	line(cr, t->right - 1, t->top + 2, t->right - 1, t->bottom,
		tint(tab, (kDarken1Tint + kNoTint) / 2));

	fill_gradient(cr, make_rect(t->left + 2, t->top + 2, t->right - 2,
		t->bottom), t->left, t->top, t->left, t->bottom,
		tab_light(style, active), tab);
}


static void
draw_title(cairo_t *cr, const struct gonx_layout *l,
	   const struct gonx_style *style, const char *title, int active)
{
	if (title == NULL || title[0] == '\0' || l->title_space <= 0)
		return;

	const struct gonx_rect *t = &l->title;
	PangoLayout *layout = create_layout(style, l->small, title);
	pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_MIDDLE);
	pango_layout_set_width(layout, l->title_space * PANGO_SCALE);

	double ascent, descent;
	font_height(style, l->small, &ascent, &descent);
	const int baseline = (int)floor(((t->top + 2.0) + t->bottom + ascent
		+ descent) / 2.0 - descent + 0.5);
	const int layout_baseline = pango_layout_get_baseline(layout) / PANGO_SCALE;

	set_source(cr, rgb(active ? style->text : style->text_inactive));
	cairo_move_to(cr, t->left + l->text_offset, baseline - layout_baseline);
	pango_cairo_update_context(cr, pango_layout_get_context(layout));
	pango_layout_context_changed(layout);
	pango_cairo_show_layout(cr, layout);

	g_object_unref(layout);
}


/* DefaultDecorator::_DrawBlendedRect() */
static void
draw_blended_rect(cairo_t *cr, struct gonx_rect r,
		  const struct gonx_style *style, int active, int down)
{
	const struct color button = tab_color(style, active);
	const struct color start = down ? tint(button, kDarken1Tint)
		: tint(button, kLightenMaxTint);
	const struct color end = down ? tab_light(style, active) : button;

	const struct gonx_rect inner = make_rect(r.left + 1, r.top + 1,
		r.right - 1, r.bottom - 1);
	fill_gradient(cr, inner, inner.left, inner.top, inner.right, inner.bottom,
		start, end);

	const struct color outline = tint(button, kDarken2Tint);
	line(cr, r.left, r.top, r.right, r.top, outline);
	line(cr, r.left, r.bottom, r.right, r.bottom, outline);
	line(cr, r.left, r.top, r.left, r.bottom, outline);
	line(cr, r.right, r.top, r.right, r.bottom, outline);
}


static void
draw_buttons(cairo_t *cr, const struct gonx_layout *l,
	     const struct gonx_style *style, int active,
	     const int pressed[GONX_BUTTON_COUNT])
{
	const struct gonx_rect *close = &l->button[GONX_CLOSE];
	if (valid(close))
		draw_blended_rect(cr, *close, style, active, pressed[GONX_CLOSE]);

	/* zoom: a large square at the bottom right under a small one at the
	 * top left */
	const struct gonx_rect *zoom = &l->button[GONX_ZOOM];
	if (!valid(zoom) || zoom->right - zoom->left < 1)
		return;

	const int width = zoom->right - zoom->left;
	const int large = (int)floor(width / 4.0);
	const int small = (int)floor(width / 2.1);
	draw_blended_rect(cr, make_rect(zoom->left + large, zoom->top + large,
		zoom->right, zoom->bottom), style, active, pressed[GONX_ZOOM]);
	draw_blended_rect(cr, make_rect(zoom->left, zoom->top,
		zoom->right - small, zoom->bottom - small), style, active,
		pressed[GONX_ZOOM]);
}


//	#pragma mark - public


void
haiku_frame_paint(cairo_t *cr, const struct gonx_layout *l,
		  const struct gonx_style *style, const char *title, int active,
		  const int pressed[GONX_BUTTON_COUNT])
{
	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_rgba(cr, 0, 0, 0, 0);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);

	draw_frame(cr, l, style, active);
	draw_resize_knob(cr, l, style, active);

	if (valid(&l->title)) {
		draw_tab(cr, l, style, active);
		cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
		draw_title(cr, l, style, title, active);
		cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
		draw_buttons(cr, l, style, active, pressed);
	}

	cairo_restore(cr);
}


void
haiku_frame_paint_overlay(cairo_t *cr, const struct gonx_layout *l,
			  const struct gonx_style *style, int active)
{
	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
	draw_resize_knob(cr, l, style, active);
	cairo_restore(cr);
}
