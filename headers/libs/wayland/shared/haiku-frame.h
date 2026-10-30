/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Haiku's window frame, drawn client-side. gonx-frame.c hands a style with
 * `haiku` set to these; frame.c only calls the gonx_frame_*() functions.
 */
#ifndef HAIKU_FRAME_H
#define HAIKU_FRAME_H

#include "gonx-frame.h"

#ifdef __cplusplus
extern "C" {
#endif

void
haiku_frame_sizes(const struct gonx_style *style, int small, int tabless,
		  int *top, int *bottom, int *left, int *right);

void
haiku_frame_layout(struct gonx_layout *layout, const struct gonx_style *style,
		   const char *title);

void
haiku_frame_paint(cairo_t *cr, const struct gonx_layout *layout,
		  const struct gonx_style *style, const char *title, int active,
		  const int pressed[GONX_BUTTON_COUNT]);

void
haiku_frame_paint_overlay(cairo_t *cr, const struct gonx_layout *layout,
			  const struct gonx_style *style, int active);

#ifdef __cplusplus
}
#endif

#endif
