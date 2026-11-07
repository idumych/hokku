/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Hokku: a DrawingEngine into a bitmap of its own, which decorators draw
 * their buttons in once and keep as UtilityBitmaps.
 */
#ifndef BITMAP_DRAWING_ENGINE_H
#define BITMAP_DRAWING_ENGINE_H


#include "DrawingEngine.h"
#include "ServerBitmap.h"


class BitmapDrawingEngine : public DrawingEngine {
public:
								BitmapDrawingEngine(
									color_space colorSpace = B_RGB32);
	virtual						~BitmapDrawingEngine();

			status_t			SetSize(int32 newWidth, int32 newHeight);
			UtilityBitmap*		ExportToBitmap(int32 width, int32 height,
									color_space space);

private:
			BBitmap*			fBitmap;
};


#endif	// BITMAP_DRAWING_ENGINE_H
