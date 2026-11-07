/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Hokku: the part of app_server's drawing state that decorators keep, their
 * font and colours, for DrawingEngine::SetDrawState().
 */
#ifndef DRAW_STATE_H
#define DRAW_STATE_H


#include <GraphicsDefs.h>
#include <InterfaceDefs.h>
#include <ObjectList.h>
#include <Point.h>

#include "PatternHandler.h"
#include "ServerFont.h"


class DrawState {
public:
							DrawState();
							DrawState(const DrawState& other);
	virtual					~DrawState();

			void			SetOrigin(BPoint origin) { fOrigin = origin; }
			BPoint			Origin() const { return fOrigin; }
			void			SetScale(float scale) { fScale = scale; }
			float			Scale() const { return fScale; }

			void			SetHighColor(rgb_color color)
								{ fHighColor = color; }
			rgb_color		HighColor() const { return fHighColor; }
			void			SetLowColor(rgb_color color)
								{ fLowColor = color; }
			rgb_color		LowColor() const { return fLowColor; }
			void			SetHighUIColor(color_which which, float tint)
								{ fHighColor = tint_color(ui_color(which),
									tint); }
			void			SetLowUIColor(color_which which, float tint)
								{ fLowColor = tint_color(ui_color(which),
									tint); }

			void			SetPattern(const Pattern& pattern)
								{ fPattern = pattern; }
			const Pattern&	GetPattern() const { return fPattern; }

			bool			SetDrawingMode(drawing_mode mode)
								{ fDrawingMode = mode; return true; }
			drawing_mode	GetDrawingMode() const { return fDrawingMode; }

			void			SetBlendingMode(source_alpha srcMode,
								alpha_function fncMode)
								{ fAlphaSourceMode = srcMode;
								  fAlphaFunctionMode = fncMode; }
			source_alpha	AlphaSrcMode() const { return fAlphaSourceMode; }
			alpha_function	AlphaFncMode() const
								{ return fAlphaFunctionMode; }

			void			SetPenSize(float size) { fPenSize = size; }
			float			PenSize() const { return fPenSize; }

			void			SetFont(const ServerFont& font,
								uint32 flags = B_FONT_ALL);
			const ServerFont& Font() const { return fFont; }

private:
			BPoint			fOrigin;
			float			fScale;
			rgb_color		fHighColor;
			rgb_color		fLowColor;
			Pattern			fPattern;
			drawing_mode	fDrawingMode;
			source_alpha	fAlphaSourceMode;
			alpha_function	fAlphaFunctionMode;
			float			fPenSize;
			ServerFont		fFont;
};


#endif	// DRAW_STATE_H
