/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Hokku: app_server's bitmaps as decorators use them, the images of their
 * buttons that a BitmapDrawingEngine draws. The pixels are a cairo surface,
 * premultiplied, so what is transparent stays so when it is drawn.
 */
#ifndef SERVER_BITMAP_H
#define SERVER_BITMAP_H


#include <GraphicsDefs.h>
#include <Rect.h>
#include <Referenceable.h>


typedef struct _cairo_surface cairo_surface_t;


class ServerBitmap : public BReferenceable {
public:
								ServerBitmap(cairo_surface_t* surface);
	virtual						~ServerBitmap();

			BRect				Bounds() const
									{ return BRect(0, 0, fWidth - 1,
										fHeight - 1); }
			int32				Width() const { return fWidth; }
			int32				Height() const { return fHeight; }
			color_space			ColorSpace() const { return B_RGBA32; }
			uint8*				Bits() const;
			uint32				BytesPerRow() const;

			cairo_surface_t*	Surface() const { return fSurface; }

protected:
			cairo_surface_t*	fSurface;
			int32				fWidth;
			int32				fHeight;
};


class UtilityBitmap : public ServerBitmap {
public:
								UtilityBitmap(cairo_surface_t* surface)
									: ServerBitmap(surface) {}
};


#endif	// SERVER_BITMAP_H
