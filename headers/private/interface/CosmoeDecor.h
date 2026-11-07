/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Hokku: window frames drawn by app_server decorators. libdecorator builds
 * Haiku's decorator add-ons from unchanged sources and runs them in the
 * application; the backend's frame code drives a decorator through these
 * calls, in the frame's coordinates (its surface's, shadow included).
 */
#ifndef COSMOE_DECOR_H
#define COSMOE_DECOR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _cairo cairo_t;
typedef struct cosmoe_decor cosmoe_decor;

/* Decorator::Region */
enum {
	COSMOE_DECOR_REGION_NONE = 0,
	COSMOE_DECOR_REGION_TAB,
	COSMOE_DECOR_REGION_CLOSE_BUTTON,
	COSMOE_DECOR_REGION_ZOOM_BUTTON,
	COSMOE_DECOR_REGION_MINIMIZE_BUTTON,
	COSMOE_DECOR_REGION_LEFT_BORDER,
	COSMOE_DECOR_REGION_RIGHT_BORDER,
	COSMOE_DECOR_REGION_TOP_BORDER,
	COSMOE_DECOR_REGION_BOTTOM_BORDER,
	COSMOE_DECOR_REGION_LEFT_TOP_CORNER,
	COSMOE_DECOR_REGION_LEFT_BOTTOM_CORNER,
	COSMOE_DECOR_REGION_RIGHT_TOP_CORNER,
	COSMOE_DECOR_REGION_RIGHT_BOTTOM_CORNER
};

/* window flags, as BWindow has them */
enum {
	COSMOE_DECOR_NOT_RESIZABLE = 0x00000002,
	COSMOE_DECOR_NOT_CLOSABLE = 0x00000020,
	COSMOE_DECOR_NOT_ZOOMABLE = 0x00000040,
	COSMOE_DECOR_NOT_MINIMIZABLE = 0x00004000
};

/* An inclusive pixel rectangle, like BRect; empty when right < left. */
struct cosmoe_decor_rect {
	int left, top, right, bottom;
};

struct cosmoe_decor_ops {
	/* A frame for a window of this window_look and these flags, or NULL
	 * when the decorator has none for the look. */
	cosmoe_decor *(*create)(uint32_t look, uint32_t flags,
				const char *title);
	void (*destroy)(cosmoe_decor *decor);

	void (*set_title)(cosmoe_decor *decor, const char *title);
	void (*set_flags)(cosmoe_decor *decor, uint32_t flags);
	void (*set_focus)(cosmoe_decor *decor, int focused);
	/* whether the window fills the screen: no shadow then */
	void (*set_maximized)(cosmoe_decor *decor, int maximized);

	/* How far the frame reaches past the content on each side, tab
	 * included, and how far its shadow or glow reaches past that. */
	void (*borders)(cosmoe_decor *decor, int *left, int *top, int *right,
			int *bottom);
	void (*shadow_extent)(cosmoe_decor *decor, int *left, int *top,
			      int *right, int *bottom);

	/* Puts the content at (x, y), width by height pixels. */
	void (*layout)(cosmoe_decor *decor, int x, int y, int width,
		       int height);

	/* A COSMOE_DECOR_REGION_* for the point */
	int (*region_at)(cosmoe_decor *decor, int x, int y);
	/* where a button region is; empty when there's no such button */
	void (*button_rect)(cosmoe_decor *decor, int region,
			    struct cosmoe_decor_rect *rect);
	void (*set_pressed)(cosmoe_decor *decor, int region, int pressed);

	/* Calls add() for each rectangle of what the frame covers. */
	void (*footprint)(cosmoe_decor *decor,
			  void (*add)(void *data, int x, int y, int width,
				      int height),
			  void *data);

	/* The frame and its shadow, onto a clear surface. */
	void (*paint)(cosmoe_decor *decor, cairo_t *cr);
	/* After the content is drawn: cuts off the rounded corners where
	 * they lie over it. */
	void (*paint_overlay)(cosmoe_decor *decor, cairo_t *cr);

	/* The border round the content and the tab, for a compositor that
	 * lays out its own input areas; the tab is empty without one. */
	void (*frame_rects)(cosmoe_decor *decor, struct cosmoe_decor_rect *border,
			    struct cosmoe_decor_rect *tab);
};

/* in libdecorator: the calls for the decorator called name ("Default" is
 * Haiku's), loading it if need be; NULL if there's no such decorator */
typedef const struct cosmoe_decor_ops *(*cosmoe_decor_load_func)(
	const char *name);
const struct cosmoe_decor_ops *cosmoe_decor_load(const char *name);

/* For a program that isn't a BApplication, such as a compositor drawing
 * other programs' frames: sets up what libbe needs to draw, from the user's
 * settings. Call it once, before cosmoe_decor_load(); 0 when it worked. */
typedef int (*cosmoe_decor_init_func)(void);
int cosmoe_decor_init_standalone(void);

/* Reads the colours, fonts and window settings (shadows, corners) again,
 * after another program changed them. Frames take them when they are next
 * painted. */
typedef void (*cosmoe_decor_reload_func)(void);
void cosmoe_decor_reload_settings(void);

/* The decorator the user's settings name, as setdecor and Appearance set
 * it ("Default" is the desktop's own frames, "Haiku" Haiku's), into name. */
typedef void (*cosmoe_decor_current_func)(char *name, size_t size);
void cosmoe_decor_current(char *name, size_t size);

/* The folder those settings are kept in, to watch for changes. */
typedef void (*cosmoe_decor_settings_folder_func)(char *path, size_t size);
void cosmoe_decor_settings_folder(char *path, size_t size);

#ifdef __cplusplus
}
#endif

#endif
