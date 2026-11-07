/*
 * Copyright 2006, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */
#ifndef _BITMAP_PRIVATE_H
#define _BITMAP_PRIVATE_H


#include <Bitmap.h>
#include <OS.h>


// This structure is placed in the client/server shared memory area.

struct overlay_client_data {
	sem_id	lock;
	uint8*	buffer;
};



class BBitmap::Private {
public:
								Private(BBitmap* bitmap);

			// Hokku: what the bitmap's views draw into, premultiplied ARGB,
			// or NULL when it doesn't accept views. Its alpha is kept, unlike
			// in Bits(). Lock the bitmap while using it.
			struct _cairo_surface*	ViewSurface() const;

private:
			BBitmap*			fBitmap;
};

#endif // _BITMAP_PRIVATE_H
