/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 *
 * The Gonx window frame, drawn client-side: a port of the Gonx app_server
 * decorator (github.com/idumych/gonx) to cairo. frame.c lays out and draws
 * a frame with these functions instead of the Weston theme when a window
 * has a Gonx style set (frame_set_gonx()).
 *
 * A style with `haiku` set asks for Haiku's own frame instead (the yellow
 * tab of app_server's Default decorator, haiku-frame.c) through the same
 * functions and layout.
 */
#ifndef GONX_FRAME_H
#define GONX_FRAME_H

#include <stdint.h>
#include <cairo.h>

#ifdef __cplusplus
extern "C" {
#endif

/* What the system's settings say the frame should look like. */
struct gonx_style {
	uint8_t tab[3];
	uint8_t tab_inactive[3];
	uint8_t text[3];
	uint8_t text_inactive[3];
	/* Pango font descriptions, e.g. "Noto Sans Bold 11px" */
	char title_font[128];
	char small_title_font[128];
	/* B_WINDOW_BORDER_COLOR and its inactive twin; only Haiku's frame uses
	 * them */
	uint8_t frame[3];
	uint8_t frame_inactive[3];
	int haiku;			/* Haiku's frame instead of Gonx */
	int shadow;			/* windows cast a shadow */
};

enum {
	GONX_CLOSE = 0,
	GONX_ZOOM = 1,
	GONX_MINIMIZE = 2,
	GONX_BUTTON_COUNT = 3
};

/* An inclusive pixel rectangle, like BRect; empty when right < left. */
struct gonx_rect {
	int left, top, right, bottom;
};

struct gonx_layout {
	/* in: the whole surface and the window's kind */
	int width, height;
	int small;			/* floating window: smaller frame, plain title */
	int tabless;		/* modal window: the border alone, no tab */
	int document;		/* document window: resize knob in the corner */
	int resizable;
	int has_button[GONX_BUTTON_COUNT];

	/* out */
	struct gonx_rect content;
	struct gonx_rect border;	/* the frame round the content */
	struct gonx_rect title;		/* the tab */
	struct gonx_rect shelf;		/* the raised strip holding the buttons */
	struct gonx_rect flap;
	struct gonx_rect button[GONX_BUTTON_COUNT];
	struct gonx_rect resize;

	int side, band, tab_height, tab_gap, flap_width, flap_depth;
	int tab_radius, tab_right_radius, tab_foot_radius, frame_radius;
	int title_padding, button_size, button_gap, shelf_margin, shelf_rise;
	int slope, text_offset, resize_length;
	int text_ascent, text_height;
	double scale;

	/* Haiku's frame: set by its layout, which leaves the shelf and flap
	 * empty. text_offset is then from the tab's left edge. */
	int haiku;
	int title_space;
};

/* How far the drop shadow reaches past the frame's right and bottom edges,
 * of a light above and to the left. Nothing reaches past the top and left. */
void
gonx_frame_shadow_extent(int *right, int *bottom);

/* Decoration thickness round the content, before the window is laid out. */
void
gonx_frame_sizes(const struct gonx_style *style, int small, int tabless,
		 int *top, int *bottom, int *left, int *right);

/* Fills in the out part of layout from its in part. */
void
gonx_frame_layout(struct gonx_layout *layout, const struct gonx_style *style,
		  const char *title);

void
gonx_frame_paint(cairo_t *cr, const struct gonx_layout *layout,
		 const struct gonx_style *style, const char *title, int active,
		 const int pressed[GONX_BUTTON_COUNT]);

/* The parts of the frame that lie over the content's edge: its rounded
 * corners and a document window's resize knob. Draw after the content. */
void
gonx_frame_paint_overlay(cairo_t *cr, const struct gonx_layout *layout,
			 const struct gonx_style *style, int active);

/* Whether (x, y) is on a part of the frame that lies over the content and
 * takes the pointer there: a document window's resize knob. */
int
gonx_frame_overlay_contains(const struct gonx_layout *layout, int x, int y);

/* A THEME_LOCATION_* value for the point (x, y). */
int
gonx_frame_location(const struct gonx_layout *layout, int x, int y);

/* Calls add() for each rectangle of the frame that takes input: everything
 * drawn, but not the transparent parts round the tab and flap. */
void
gonx_frame_input_rects(const struct gonx_layout *layout,
		       void (*add)(void *data, int x, int y, int width,
				   int height),
		       void *data);

/* The drop shadow the frame casts on what is below it, painted under what
 * the surface already holds (after gonx_frame_paint()): the frame's shape and
 * the content's, moved down and right and blurred. It is painted where it can
 * show, round the content and out past the frame's right and bottom edges as
 * far as gonx_frame_shadow_extent() says, and only within clip (frame
 * coordinates) when clip isn't NULL. The surface must reach that far. */
void
gonx_frame_paint_shadow(cairo_t *cr, const struct gonx_layout *layout,
			const struct gonx_style *style,
			const struct gonx_rect *clip);

/* The same in two steps, for keeping the shadow while the layout stays:
 * its alpha, which goes at (x, y) in frame coordinates (NULL when there is
 * nothing to paint), then painting it. */
cairo_surface_t *
gonx_frame_create_shadow(const struct gonx_layout *layout,
			 const struct gonx_style *style,
			 const struct gonx_rect *clip, int *x, int *y);

void
gonx_frame_paint_shadow_surface(cairo_t *cr, const struct gonx_layout *layout,
				cairo_surface_t *shadow, int x, int y);

#ifdef __cplusplus
}
#endif

#endif
