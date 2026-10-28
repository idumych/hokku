/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef ICON_LOADER_H
#define ICON_LOADER_H


#include <string>

#include <cairo.h>
#include <glib.h>


class BBitmap;


namespace IconLoader {

// Finds an icon file by freedesktop icon name: first in extraPath (an
// item's IconThemePath, which may be a flat folder or a theme root), then in
// the user's icon theme and the themes it inherits, then hicolor. Returns ""
// when nothing matches.
std::string			FindIcon(const char* name, int size,
						const char* extraPath = NULL);

// Loads a PNG or (with librsvg) SVG file, or an icon by name, as a cairo
// surface. The caller owns the result; NULL on failure.
cairo_surface_t*	LoadFile(const char* path, int size);
cairo_surface_t*	LoadNamed(const char* name, int size,
						const char* extraPath = NULL);

// StatusNotifierItem pixmaps: a(iiay), ARGB32 in network byte order. Picks
// the smallest pixmap at least size wide, else the largest.
cairo_surface_t*	LoadPixmaps(GVariant* pixmaps, int size);

// PNG bytes, as in a dbusmenu "icon-data" property.
cairo_surface_t*	LoadPNGData(const void* data, size_t length);

// Scales a surface into a size x size B_RGBA32 bitmap, keeping its aspect
// ratio and centering it, with overlay (if any) in the lower right quarter.
// Takes ownership of neither surface.
BBitmap*			MakeBitmap(cairo_surface_t* icon, int size,
						cairo_surface_t* overlay = NULL);

}	// namespace IconLoader


#endif	// ICON_LOADER_H
