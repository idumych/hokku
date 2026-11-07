/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Hokku: app_server's DrawingEngine for decorators, drawing through a BView
 * of an off-screen bitmap.
 */


#include "DrawingEngine.h"

#include <new>
#include <stdio.h>
#include <string.h>

#include <cairo.h>

#include <Bitmap.h>
#include <View.h>

#include <BitmapPrivate.h>

#include "BitmapDrawingEngine.h"
#include "DrawState.h"
#include "ServerBitmap.h"
#include "ServerFont.h"


/*!	Draws in a color of its own, as the rgb_color versions of app_server's
	calls do, and puts the view's color back afterwards.
*/
class DrawingEngine::SolidColor {
public:
	SolidColor(BView* view, const rgb_color& color)
		:
		fView(view)
	{
		fOldColor = fView->HighColor();
		fView->SetHighColor(color);
	}

	~SolidColor()
	{
		fView->SetHighColor(fOldColor);
	}

private:
	BView*		fView;
	rgb_color	fOldColor;
};


DrawingEngine::DrawingEngine()
	:
	fBitmap(NULL),
	fView(NULL),
	fPattern(B_SOLID_HIGH),
	fCopyToFront(true),
	fFont(be_plain_font)
{
}


DrawingEngine::~DrawingEngine()
{
}


void
DrawingEngine::SetTarget(BBitmap* bitmap, BView* view)
{
	fBitmap = bitmap;
	fView = view;
	if (fView != NULL)
		fView->SetFont(&fFont);
}


void
DrawingEngine::ConstrainClippingRegion(const BRegion* region)
{
	if (fView == NULL)
		return;
	BRegion copy;
	if (region != NULL)
		copy = *region;
	fView->ConstrainClippingRegion(region != NULL ? &copy : NULL);
}


void
DrawingEngine::SetDrawState(const DrawState* state, int32 xOffset,
	int32 yOffset)
{
	if (state == NULL)
		return;

	fPattern = state->GetPattern().GetPattern();
	_SetFont(state->Font().GetBFont());
	if (fView == NULL)
		return;

	fView->SetHighColor(state->HighColor());
	fView->SetLowColor(state->LowColor());
	fView->SetDrawingMode(state->GetDrawingMode());
	fView->SetBlendingMode(state->AlphaSrcMode(), state->AlphaFncMode());
	fView->SetPenSize(state->PenSize());
}


void
DrawingEngine::SetHighColor(const rgb_color& color)
{
	if (fView != NULL)
		fView->SetHighColor(color);
}


void
DrawingEngine::SetLowColor(const rgb_color& color)
{
	if (fView != NULL)
		fView->SetLowColor(color);
}


void
DrawingEngine::SetPenSize(float size)
{
	if (fView != NULL)
		fView->SetPenSize(size);
}


void
DrawingEngine::SetStrokeMode(cap_mode lineCap, join_mode joinMode,
	float miterLimit)
{
	if (fView != NULL)
		fView->SetLineMode(lineCap, joinMode, miterLimit);
}


void
DrawingEngine::SetFillRule(int32 fillRule)
{
	if (fView != NULL)
		fView->SetFillRule(fillRule);
}


void
DrawingEngine::SetPattern(const struct pattern& pattern)
{
	fPattern = pattern;
}


void
DrawingEngine::SetDrawingMode(drawing_mode mode)
{
	if (fView != NULL)
		fView->SetDrawingMode(mode);
}


void
DrawingEngine::SetDrawingMode(drawing_mode mode, drawing_mode& oldMode)
{
	if (fView == NULL) {
		oldMode = mode;
		return;
	}
	oldMode = fView->DrawingMode();
	fView->SetDrawingMode(mode);
}


void
DrawingEngine::SetBlendingMode(source_alpha srcAlpha,
	alpha_function alphaFunc)
{
	if (fView != NULL)
		fView->SetBlendingMode(srcAlpha, alphaFunc);
}


void
DrawingEngine::SetFont(const ServerFont& font)
{
	_SetFont(font.GetBFont());
}


void
DrawingEngine::SetFont(const DrawState* state)
{
	if (state != NULL)
		_SetFont(state->Font().GetBFont());
}


void
DrawingEngine::SetTransform(const BAffineTransform& transform, int32 xOffset,
	int32 yOffset)
{
	// Decorators draw in the view's own coordinates, and only reset this.
	if (fView != NULL)
		fView->SetTransform(transform);
}


void
DrawingEngine::InvertRect(BRect rect)
{
	if (fView != NULL)
		fView->InvertRect(rect);
}


void
DrawingEngine::DrawBitmap(ServerBitmap* bitmap, const BRect& bitmapRect,
	const BRect& viewRect, uint32 options)
{
	if (fView == NULL || fBitmap == NULL || bitmap == NULL)
		return;

	cairo_surface_t* target = BBitmap::Private(fBitmap).ViewSurface();
	if (target == NULL)
		return;

	// The view's drawing is done by the time a call returns; this draws on
	// the same pixels in between. The surface may have more pixels than the
	// view has, on a scaled display.
	const double scale = (double)cairo_image_surface_get_width(target)
		/ (fBitmap->Bounds().IntegerWidth() + 1);

	cairo_surface_flush(target);
	cairo_t* cr = cairo_create(target);
	cairo_scale(cr, scale, scale);
	cairo_rectangle(cr, viewRect.left, viewRect.top, viewRect.Width() + 1,
		viewRect.Height() + 1);
	cairo_clip(cr);
	cairo_translate(cr, viewRect.left, viewRect.top);
	cairo_scale(cr, (viewRect.Width() + 1) / (bitmapRect.Width() + 1),
		(viewRect.Height() + 1) / (bitmapRect.Height() + 1));
	cairo_set_source_surface(cr, bitmap->Surface(), -bitmapRect.left,
		-bitmapRect.top);
	if (viewRect.Width() != bitmapRect.Width()
		|| viewRect.Height() != bitmapRect.Height()) {
		cairo_pattern_set_filter(cairo_get_source(cr),
			(options & B_FILTER_BITMAP_BILINEAR) != 0
				? CAIRO_FILTER_BILINEAR : CAIRO_FILTER_NEAREST);
	}
	// The bitmaps are premultiplied, transparent where app_server's would
	// be B_TRANSPARENT_COLOR, which every mode leaves out.
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	cairo_paint(cr);
	cairo_destroy(cr);
	cairo_surface_mark_dirty(target);
}


void
DrawingEngine::DrawArc(BRect rect, const float& angle, const float& span,
	bool filled)
{
	if (fView == NULL)
		return;
	if (filled)
		fView->FillArc(rect, angle, span, fPattern);
	else
		fView->StrokeArc(rect, angle, span, fPattern);
}


void
DrawingEngine::DrawArc(BRect rect, const float& angle, const float& span,
	bool filled, const BGradient& gradient)
{
	if (fView == NULL)
		return;
	if (filled)
		fView->FillArc(rect, angle, span, gradient);
	else
		fView->StrokeArc(rect, angle, span, gradient);
}


void
DrawingEngine::DrawBezier(BPoint* points, bool filled)
{
	if (fView == NULL)
		return;
	if (filled)
		fView->FillBezier(points, fPattern);
	else
		fView->StrokeBezier(points, fPattern);
}


void
DrawingEngine::DrawBezier(BPoint* points, bool filled,
	const BGradient& gradient)
{
	if (fView == NULL)
		return;
	if (filled)
		fView->FillBezier(points, gradient);
	else
		fView->StrokeBezier(points, gradient);
}


void
DrawingEngine::DrawEllipse(BRect rect, bool filled)
{
	if (fView == NULL)
		return;
	if (filled)
		fView->FillEllipse(rect, fPattern);
	else
		fView->StrokeEllipse(rect, fPattern);
}


void
DrawingEngine::DrawEllipse(BRect rect, bool filled, const BGradient& gradient)
{
	if (fView == NULL)
		return;
	if (filled)
		fView->FillEllipse(rect, gradient);
	else
		fView->StrokeEllipse(rect, gradient);
}


void
DrawingEngine::DrawPolygon(BPoint* points, int32 count, BRect bounds,
	bool filled, bool closed)
{
	if (fView == NULL)
		return;
	if (filled)
		fView->FillPolygon(points, count, bounds, fPattern);
	else
		fView->StrokePolygon(points, count, bounds, closed, fPattern);
}


void
DrawingEngine::DrawPolygon(BPoint* points, int32 count, BRect bounds,
	bool filled, bool closed, const BGradient& gradient)
{
	if (fView == NULL)
		return;
	if (filled)
		fView->FillPolygon(points, count, bounds, gradient);
	else
		fView->StrokePolygon(points, count, bounds, closed, gradient);
}


void
DrawingEngine::StrokePoint(const BPoint& point, const rgb_color& color)
{
	StrokeLine(point, point, color);
}


void
DrawingEngine::StrokeRect(BRect rect, const rgb_color& color)
{
	if (fView == NULL)
		return;
	SolidColor solid(fView, color);
	fView->StrokeRect(rect);
}


void
DrawingEngine::FillRect(BRect rect, const rgb_color& color)
{
	if (fView == NULL)
		return;
	SolidColor solid(fView, color);
	fView->FillRect(rect);
}


void
DrawingEngine::FillRegion(BRegion& region, const rgb_color& color)
{
	if (fView == NULL)
		return;
	SolidColor solid(fView, color);
	fView->FillRegion(&region);
}


void
DrawingEngine::StrokeRect(BRect rect)
{
	if (fView != NULL)
		fView->StrokeRect(rect, fPattern);
}


void
DrawingEngine::StrokeRect(BRect rect, const BGradient& gradient)
{
	if (fView != NULL)
		fView->StrokeRect(rect, gradient);
}


void
DrawingEngine::FillRect(BRect rect)
{
	if (fView != NULL)
		fView->FillRect(rect, fPattern);
}


void
DrawingEngine::FillRect(BRect rect, const BGradient& gradient)
{
	if (fView != NULL)
		fView->FillRect(rect, gradient);
}


void
DrawingEngine::FillRegion(BRegion& region)
{
	if (fView != NULL)
		fView->FillRegion(&region, fPattern);
}


void
DrawingEngine::FillRegion(BRegion& region, const BGradient& gradient)
{
	if (fView != NULL)
		fView->FillRegion(&region, gradient);
}


void
DrawingEngine::DrawRoundRect(BRect rect, float xRadius, float yRadius,
	bool filled)
{
	if (fView == NULL)
		return;
	if (filled)
		fView->FillRoundRect(rect, xRadius, yRadius, fPattern);
	else
		fView->StrokeRoundRect(rect, xRadius, yRadius, fPattern);
}


void
DrawingEngine::DrawRoundRect(BRect rect, float xRadius, float yRadius,
	bool filled, const BGradient& gradient)
{
	if (fView == NULL)
		return;
	if (filled)
		fView->FillRoundRect(rect, xRadius, yRadius, gradient);
	else
		fView->StrokeRoundRect(rect, xRadius, yRadius, gradient);
}


void
DrawingEngine::DrawTriangle(BPoint* points, const BRect& bounds, bool filled)
{
	if (fView == NULL)
		return;
	if (filled)
		fView->FillTriangle(points[0], points[1], points[2], bounds, fPattern);
	else {
		fView->StrokeTriangle(points[0], points[1], points[2], bounds,
			fPattern);
	}
}


void
DrawingEngine::DrawTriangle(BPoint* points, const BRect& bounds, bool filled,
	const BGradient& gradient)
{
	if (fView == NULL)
		return;
	if (filled)
		fView->FillTriangle(points[0], points[1], points[2], bounds, gradient);
	else {
		fView->StrokeTriangle(points[0], points[1], points[2], bounds,
			gradient);
	}
}


void
DrawingEngine::StrokeLine(const BPoint& start, const BPoint& end,
	const rgb_color& color)
{
	if (fView == NULL)
		return;
	SolidColor solid(fView, color);
	fView->StrokeLine(start, end);
}


void
DrawingEngine::StrokeLine(const BPoint& start, const BPoint& end)
{
	if (fView != NULL)
		fView->StrokeLine(start, end, fPattern);
}


void
DrawingEngine::StrokeLine(const BPoint& start, const BPoint& end,
	const BGradient& gradient)
{
	if (fView != NULL)
		fView->StrokeLine(start, end, gradient);
}


BPoint
DrawingEngine::DrawString(const char* string, int32 length,
	const BPoint& point, escapement_delta* delta)
{
	if (fView == NULL)
		return DrawStringDry(string, length, point, delta);
	fView->DrawString(string, length, point, delta);
	return fView->PenLocation();
}


BPoint
DrawingEngine::DrawString(const char* string, int32 length,
	const BPoint* offsets)
{
	if (fView == NULL || length <= 0)
		return offsets != NULL ? offsets[0] : BPoint();
	fView->DrawString(string, length, offsets, length);
	return fView->PenLocation();
}


float
DrawingEngine::StringWidth(const char* string, int32 length,
	escapement_delta* delta)
{
	if (string == NULL || length <= 0)
		return 0.0f;
	return fFont.StringWidth(string, length);
}


float
DrawingEngine::StringWidth(const char* string, int32 length,
	const ServerFont& font, escapement_delta* delta)
{
	return font.StringWidth(string, length, delta);
}


BPoint
DrawingEngine::DrawStringDry(const char* string, int32 length,
	const BPoint& point, escapement_delta* delta)
{
	return BPoint(point.x + StringWidth(string, length, delta), point.y);
}


void
DrawingEngine::_SetFont(const BFont& font)
{
	fFont = font;
	if (fView != NULL)
		fView->SetFont(&fFont);
}


// #pragma mark - BitmapDrawingEngine


BitmapDrawingEngine::BitmapDrawingEngine(color_space colorSpace)
	:
	fBitmap(NULL)
{
}


BitmapDrawingEngine::~BitmapDrawingEngine()
{
	SetTarget(NULL, NULL);
	if (fBitmap != NULL && fBitmap->IsLocked())
		fBitmap->Unlock();
	delete fBitmap;
}


status_t
BitmapDrawingEngine::SetSize(int32 newWidth, int32 newHeight)
{
	if (newWidth <= 0 || newHeight <= 0)
		return B_BAD_VALUE;

	// A decorator sets the size, draws, and exports what it drew: the bitmap
	// stays locked from here until then.
	if (fBitmap != NULL && fBitmap->Bounds().IntegerWidth() + 1 == newWidth
		&& fBitmap->Bounds().IntegerHeight() + 1 == newHeight) {
		if (!fBitmap->IsLocked())
			fBitmap->Lock();
		// start over clear, as app_server's would
		cairo_surface_t* surface = BBitmap::Private(fBitmap).ViewSurface();
		if (surface != NULL) {
			cairo_t* cr = cairo_create(surface);
			cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
			cairo_paint(cr);
			cairo_destroy(cr);
			cairo_surface_mark_dirty(surface);
		}
		return B_OK;
	}

	SetTarget(NULL, NULL);
	if (fBitmap != NULL && fBitmap->IsLocked())
		fBitmap->Unlock();
	delete fBitmap;
	fBitmap = new(std::nothrow) BBitmap(BRect(0, 0, newWidth - 1,
		newHeight - 1), B_BITMAP_ACCEPTS_VIEWS, B_RGBA32);
	if (fBitmap == NULL || fBitmap->InitCheck() != B_OK) {
		delete fBitmap;
		fBitmap = NULL;
		return B_NO_MEMORY;
	}

	BView* view = new(std::nothrow) BView(fBitmap->Bounds(), "engine",
		B_FOLLOW_NONE, 0);
	if (view == NULL)
		return B_NO_MEMORY;
	fBitmap->Lock();
	fBitmap->AddChild(view);
	SetTarget(fBitmap, view);
	return B_OK;
}


/*!	A copy of what has been drawn. Pixels in B_TRANSPARENT_COLOR are left
	out, which is what B_OP_OVER does with them in app_server.
*/
UtilityBitmap*
BitmapDrawingEngine::ExportToBitmap(int32 width, int32 height,
	color_space space)
{
	if (fBitmap == NULL)
		return NULL;

	cairo_surface_t* surface = BBitmap::Private(fBitmap).ViewSurface();
	if (surface == NULL)
		return NULL;
	View()->Sync();
	cairo_surface_flush(surface);

	const double scale = (double)cairo_image_surface_get_width(surface)
		/ (fBitmap->Bounds().IntegerWidth() + 1);

	cairo_surface_t* copy = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
		width, height);
	cairo_t* cr = cairo_create(copy);
	cairo_scale(cr, 1 / scale, 1 / scale);
	cairo_set_source_surface(cr, surface, 0, 0);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_paint(cr);
	cairo_destroy(cr);
	cairo_surface_flush(copy);

	const rgb_color magic = B_TRANSPARENT_COLOR;
	uint8* bits = cairo_image_surface_get_data(copy);
	const int32 stride = cairo_image_surface_get_stride(copy);
	for (int32 y = 0; y < height; y++) {
		uint8* pixel = bits + y * stride;
		for (int32 x = 0; x < width; x++, pixel += 4) {
			if (pixel[3] == 255 && pixel[0] == magic.blue
				&& pixel[1] == magic.green && pixel[2] == magic.red) {
				memset(pixel, 0, 4);
			}
		}
	}
	cairo_surface_mark_dirty(copy);

	if (fBitmap->IsLocked())
		fBitmap->Unlock();

	UtilityBitmap* bitmap = new(std::nothrow) UtilityBitmap(copy);
	if (bitmap == NULL)
		cairo_surface_destroy(copy);
	return bitmap;
}
