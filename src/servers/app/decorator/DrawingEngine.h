/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Hokku: app_server's DrawingEngine as decorators draw with it, drawing
 * through a BView. The view belongs to an off-screen bitmap, so a decorator's
 * frame comes out as anything else Hokku draws does. Coordinates are the
 * view's; Hokku lays a decorator out in them.
 *
 * Only the calls decorators make are here, with app_server's signatures, so
 * that their sources build unchanged.
 */
#ifndef DRAWING_ENGINE_H
#define DRAWING_ENGINE_H


#include <AffineTransform.h>
#include <Font.h>
#include <Gradient.h>
#include <GraphicsDefs.h>
#include <Point.h>
#include <Rect.h>
#include <Region.h>


class BBitmap;
class BView;
class DrawState;
class ServerBitmap;
class ServerFont;


class DrawingEngine {
public:
								DrawingEngine();
	virtual						~DrawingEngine();

			// Hokku: what the engine draws into, a view in an off-screen
			// bitmap, or NULL for nothing. Whoever sets it keeps the bitmap
			// locked while the engine draws.
			void				SetTarget(BBitmap* bitmap, BView* view);
			BView*				View() const { return fView; }

	virtual	void				SetCopyToFrontEnabled(bool enable)
									{ fCopyToFront = enable; }
			bool				CopyToFrontEnabled() const
									{ return fCopyToFront; }
	virtual	void				CopyToFront(BRegion& region) {}

			bool				LockParallelAccess() { return true; }
			void				UnlockParallelAccess() {}
			bool				LockExclusiveAccess() { return true; }
	virtual	bool				IsExclusiveAccessLocked() const
									{ return true; }
			void				UnlockExclusiveAccess() {}

	virtual	void				ConstrainClippingRegion(const BRegion* region);

	virtual	void				SetDrawState(const DrawState* state,
									int32 xOffset = 0, int32 yOffset = 0);

	virtual	void				SetHighColor(const rgb_color& color);
	virtual	void				SetLowColor(const rgb_color& color);
	virtual	void				SetPenSize(float size);
	virtual	void				SetStrokeMode(cap_mode lineCap,
									join_mode joinMode, float miterLimit);
	virtual	void				SetFillRule(int32 fillRule);
	virtual	void				SetPattern(const struct pattern& pattern);
	virtual	void				SetDrawingMode(drawing_mode mode);
	virtual	void				SetDrawingMode(drawing_mode mode,
									drawing_mode& oldMode);
	virtual	void				SetBlendingMode(source_alpha srcAlpha,
									alpha_function alphaFunc);
	virtual	void				SetFont(const ServerFont& font);
	virtual	void				SetFont(const DrawState* state);
	virtual	void				SetTransform(const BAffineTransform& transform,
									int32 xOffset, int32 yOffset);

	virtual	void				InvertRect(BRect rect);

	virtual	void				DrawBitmap(ServerBitmap* bitmap,
									const BRect& bitmapRect,
									const BRect& viewRect, uint32 options = 0);

	virtual	void				DrawArc(BRect rect, const float& angle,
									const float& span, bool filled);
	virtual	void				DrawArc(BRect rect, const float& angle,
									const float& span, bool filled,
									const BGradient& gradient);

	virtual	void				DrawBezier(BPoint* points, bool filled);
	virtual	void				DrawBezier(BPoint* points, bool filled,
									const BGradient& gradient);

	virtual	void				DrawEllipse(BRect rect, bool filled);
	virtual	void				DrawEllipse(BRect rect, bool filled,
									const BGradient& gradient);

	virtual	void				DrawPolygon(BPoint* points, int32 count,
									BRect bounds, bool filled, bool closed);
	virtual	void				DrawPolygon(BPoint* points, int32 count,
									BRect bounds, bool filled, bool closed,
									const BGradient& gradient);

	// these rgb_color versions are used internally by the server
	virtual	void				StrokePoint(const BPoint& point,
									const rgb_color& color);
	virtual	void				StrokeRect(BRect rect, const rgb_color& color);
	virtual	void				FillRect(BRect rect, const rgb_color& color);
	virtual	void				FillRegion(BRegion& region,
									const rgb_color& color);

	virtual	void				StrokeRect(BRect rect);
	virtual	void				StrokeRect(BRect rect,
									const BGradient& gradient);
	virtual	void				FillRect(BRect rect);
	virtual	void				FillRect(BRect rect, const BGradient& gradient);

	virtual	void				FillRegion(BRegion& region);
	virtual	void				FillRegion(BRegion& region,
									const BGradient& gradient);

	virtual	void				DrawRoundRect(BRect rect, float xRadius,
									float yRadius, bool filled);
	virtual	void				DrawRoundRect(BRect rect, float xRadius,
									float yRadius, bool filled,
									const BGradient& gradient);

	virtual	void				DrawTriangle(BPoint* points,
									const BRect& bounds, bool filled);
	virtual	void				DrawTriangle(BPoint* points,
									const BRect& bounds, bool filled,
									const BGradient& gradient);

	virtual	void				StrokeLine(const BPoint& start,
									const BPoint& end, const rgb_color& color);
	virtual	void				StrokeLine(const BPoint& start,
									const BPoint& end);
	virtual	void				StrokeLine(const BPoint& start,
									const BPoint& end,
									const BGradient& gradient);

	virtual	BPoint				DrawString(const char* string, int32 length,
									const BPoint& point,
									escapement_delta* delta = NULL);
	virtual	BPoint				DrawString(const char* string, int32 length,
									const BPoint* offsets);

			float				StringWidth(const char* string, int32 length,
									escapement_delta* delta = NULL);
			float				StringWidth(const char* string, int32 length,
									const ServerFont& font,
									escapement_delta* delta = NULL);

			BPoint				DrawStringDry(const char* string,
									int32 length, const BPoint& point,
									escapement_delta* delta = NULL);

private:
			void				_SetFont(const BFont& font);

			class SolidColor;

			BBitmap*			fBitmap;
			BView*				fView;
			::pattern			fPattern;
			bool				fCopyToFront;
			BFont				fFont;
};


#endif	// DRAWING_ENGINE_H
