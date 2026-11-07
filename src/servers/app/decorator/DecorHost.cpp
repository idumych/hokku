/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Hokku: runs app_server decorators in the application, for the backend's
 * window frames (CosmoeDecor.h). Each frame has a decorator of its own, laid
 * out in the frame's coordinates, which draws into an off-screen bitmap
 * through a DrawingEngine. Its shape gives the frame its rounded corners and
 * a shadow, or a glow round it on a dark desktop, as on Renku's composited
 * desktop.
 */


#include <CosmoeDecor.h>

#include <algorithm>
#include <map>
#include <math.h>
#include <new>
#include <stdio.h>
#include <string.h>
#include <vector>

#include <cairo.h>

#include <Autolock.h>
#include <Bitmap.h>
#include <Directory.h>
#include <FindDirectory.h>
#include <Locker.h>
#include <Path.h>
#include <String.h>
#include <View.h>

#include <Application.h>

#include <BitmapPrivate.h>
#include <DecoratorPrivate.h>
#include <InterfacePrivate.h>
#include <WindowPrivate.h>

#include "DecorManager.h"
#include "DefaultDecorator.h"
#include "Desktop.h"
#include "DesktopSettings.h"
#include "DrawingEngine.h"
#include "RoundedDecorator.h"
#include "WindowShape.h"


typedef DecorAddOn* (*instantiate_decor_addon_func)(image_id id,
	const char* name);


// The shadow, as Renku's compositor casts it: the window's shape, blurred,
// a little down. On a dark desktop a glow in the panel text color instead,
// all round.
static const float kShadowSigma = 6.0f;
static const int32 kShadowOffset = 4;
static const uint8 kShadowAlpha = 96;
static const uint8 kGlowAlpha = 160;

// The bitmap a frame is drawn in grows in these steps, not to be made again
// for every pixel a window is resized by.
static const int32 kBitmapStep = 128;


static BLocker sLock("decor host");
static DecorAddOn* sAddOn = NULL;
static BString sAddOnName;
static std::map<BString, DecorAddOn*>* sAddOns = NULL;
// counts cosmoe_decor_reload_settings(): frames made before the last one
// take the new colours and fonts when they are next painted
static int32 sSettingsGeneration = 0;


struct cosmoe_decor {
	DecorAddOn*			addOn;
	Decorator*			decorator;
	DrawingEngine		engine;
	BBitmap*			bitmap;
	BView*				view;

	window_look			look;
	uint32				flags;
	BString				title;
	bool				focused;
	bool				maximized;
	bool				laidOut;
	BRect				content;
	int32				settingsGeneration;

	// the shadow's alpha, kept while the frame's shape and look stay
	cairo_surface_t*	shadow;
	int32				shadowX;
	int32				shadowY;
	std::vector<Decorator::ShapeRect> shadowShape;
	BRect				shadowFrame;
	bool				shadowGlow;
};


// #pragma mark - shapes and shadows


static void
get_shape(cosmoe_decor* decor, std::vector<Decorator::ShapeRect>& shape)
{
	Decorator* decorator = decor->decorator;
	DefaultDecorator* standard = dynamic_cast<DefaultDecorator*>(decorator);
	RoundedDecorator* rounded = dynamic_cast<RoundedDecorator*>(decorator);
	if (standard != NULL && standard->HasRoundedCorners())
		standard->GetRoundedShape(shape);
	else if (rounded != NULL && RoundedDecorator::IsActive(Desktop::Default()))
		rounded->GetRoundedShape(shape);
	else
		decorator->GetShape(shape);

	if (shape.empty()) {
		Decorator::ShapeRect rect = { decor->content, 0, 0, 0, 0 };
		shape.push_back(rect);
	}
}


static bool
has_shadow(cosmoe_decor* decor)
{
	DesktopSettings settings(Desktop::Default());
	return !decor->maximized && settings.CompositedShadows()
		&& decor->look != B_NO_BORDER_WINDOW_LOOK;
}


static bool
is_glow()
{
	return ui_color(B_PANEL_BACKGROUND_COLOR).IsDark();
}


/*!	The widths of three box blurs that make a gaussian of sigma (Ivan
	Kutskir's "boxes for gauss").
*/
static void
boxes_for_gauss(float sigma, int32 boxes[3])
{
	const float ideal = sqrtf(12 * sigma * sigma / 3 + 1);
	int32 lower = (int32)floorf(ideal);
	if (lower % 2 == 0)
		lower--;
	const int32 upper = lower + 2;
	const float mIdeal = (12 * sigma * sigma - 3 * lower * lower - 4 * 3 * lower
		- 3 * 3) / (-4.0f * lower - 4);
	const int32 m = (int32)roundf(mIdeal);
	for (int32 i = 0; i < 3; i++)
		boxes[i] = i < m ? lower : upper;
}


static int32
shadow_margin()
{
	int32 boxes[3];
	boxes_for_gauss(kShadowSigma, boxes);
	return (boxes[0] - 1) / 2 + (boxes[1] - 1) / 2 + (boxes[2] - 1) / 2;
}


static void
box_blur_line(uint8* data, int32 count, int32 step, int32 width, uint8* line)
{
	const int32 radius = (width - 1) / 2;
	for (int32 i = 0; i < count; i++)
		line[i] = data[i * step];

	int32 sum = 0;
	for (int32 i = 0; i < radius && i < count; i++)
		sum += line[i];
	for (int32 i = 0; i < count; i++) {
		if (i + radius < count)
			sum += line[i + radius];
		if (i - radius - 1 >= 0)
			sum -= line[i - radius - 1];
		data[i * step] = (uint8)((sum + width / 2) / width);
	}
}


static void
blur(cairo_surface_t* mask)
{
	cairo_surface_flush(mask);
	uint8* bits = cairo_image_surface_get_data(mask);
	const int32 width = cairo_image_surface_get_width(mask);
	const int32 height = cairo_image_surface_get_height(mask);
	const int32 stride = cairo_image_surface_get_stride(mask);

	int32 boxes[3];
	boxes_for_gauss(kShadowSigma, boxes);
	std::vector<uint8> line(std::max(width, height));
	for (int32 pass = 0; pass < 3; pass++) {
		for (int32 y = 0; y < height; y++)
			box_blur_line(bits + y * stride, width, 1, boxes[pass], &line[0]);
		for (int32 x = 0; x < width; x++)
			box_blur_line(bits + x, height, stride, boxes[pass], &line[0]);
	}
	cairo_surface_mark_dirty(mask);
}


/*!	Cuts off what the rounded corners of the shape cut off from an A8 mask
	at (maskX, maskY).
*/
static void
cut_mask_corners(cairo_surface_t* mask, int32 maskX, int32 maskY,
	const std::vector<Decorator::ShapeRect>& shape)
{
	cairo_surface_flush(mask);
	uint8* bits = cairo_image_surface_get_data(mask);
	const int32 width = cairo_image_surface_get_width(mask);
	const int32 height = cairo_image_surface_get_height(mask);
	const int32 stride = cairo_image_surface_get_stride(mask);

	BRegion all;
	for (size_t i = 0; i < shape.size(); i++)
		all.Include(shape[i].rect);
	WindowShape windowShape;
	windowShape.SetTo(shape, all, false, false, make_color(0, 0, 0));

	const std::vector<WindowShape::Corner>& corners = windowShape.Corners();
	for (size_t i = 0; i < corners.size(); i++) {
		const WindowShape::Corner& corner = corners[i];
		const uint8* coverage = WindowShape::CoverageFor(corner.radius);
		if (coverage == NULL)
			continue;
		const bool right = corner.kind == WindowShape::RIGHT_TOP
			|| corner.kind == WindowShape::RIGHT_BOTTOM;
		const bool bottom = corner.kind == WindowShape::LEFT_BOTTOM
			|| corner.kind == WindowShape::RIGHT_BOTTOM;
		for (int32 row = 0; row < corner.radius; row++) {
			for (int32 column = 0; column < corner.radius; column++) {
				const int32 px = (int32)corner.frame.left
					+ (right ? corner.radius - 1 - column : column);
				const int32 py = (int32)corner.frame.top
					+ (bottom ? corner.radius - 1 - row : row);
				// a corner covered by another rectangle of the shape stays
				if (!windowShape.EdgeRegion().Contains(BPoint(px, py)))
					continue;
				const int32 x = px - maskX;
				const int32 y = py - maskY;
				if (x < 0 || y < 0 || x >= width || y >= height)
					continue;
				const uint8 keep = coverage[row * corner.radius + column];
				bits[y * stride + x] = bits[y * stride + x] * keep / 255;
			}
		}
	}
	cairo_surface_mark_dirty(mask);
}


/*!	The shadow is cast by what the window covers: the frame as the
	decorator drew it, which may leave parts of its shape clear, and the
	content. It is made again when the shape changes.
*/
static void
update_shadow(cosmoe_decor* decor, cairo_surface_t* frameSurface,
	double scale, const BRect& frame)
{
	std::vector<Decorator::ShapeRect> shape;
	const bool shadow = has_shadow(decor);
	if (shadow)
		get_shape(decor, shape);
	const bool glow = is_glow();

	if (decor->shadow != NULL && shape == decor->shadowShape
		&& glow == decor->shadowGlow && frame == decor->shadowFrame) {
		return;
	}

	if (decor->shadow != NULL)
		cairo_surface_destroy(decor->shadow);
	decor->shadow = NULL;
	decor->shadowShape = shape;
	decor->shadowGlow = glow;
	decor->shadowFrame = frame;
	if (shape.empty() || !frame.IsValid())
		return;

	const int32 offsetY = glow ? 0 : kShadowOffset;
	const int32 margin = shadow_margin();
	const int32 width = frame.IntegerWidth() + 1 + 2 * margin;
	const int32 height = frame.IntegerHeight() + 1 + 2 * margin;
	const int32 maskX = (int32)frame.left - margin;
	const int32 maskY = (int32)frame.top - margin;

	cairo_surface_t* mask = cairo_image_surface_create(CAIRO_FORMAT_A8,
		width, height);
	if (cairo_surface_status(mask) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(mask);
		return;
	}

	// the frame's alpha and the content, where the window is
	cairo_t* cr = cairo_create(mask);
	cairo_translate(cr, -maskX, -maskY);
	cairo_save(cr);
	cairo_scale(cr, 1 / scale, 1 / scale);
	cairo_set_source_surface(cr, frameSurface, 0, 0);
	cairo_paint(cr);
	cairo_restore(cr);
	cairo_rectangle(cr, decor->content.left, decor->content.top,
		decor->content.Width() + 1, decor->content.Height() + 1);
	cairo_set_source_rgba(cr, 0, 0, 0, 1);
	cairo_fill(cr);
	cairo_destroy(cr);
	cut_mask_corners(mask, maskX, maskY, shape);

	blur(mask);
	decor->shadow = mask;
	decor->shadowX = maskX;
	decor->shadowY = maskY + offsetY;
}


/*!	Cuts off the corners of the shape from what is on the surface, by how
	much of each pixel is outside them.
*/
static void
cut_corners(cosmoe_decor* decor, cairo_t* cr)
{
	std::vector<Decorator::ShapeRect> shape;
	get_shape(decor, shape);

	BRegion all;
	for (size_t i = 0; i < shape.size(); i++)
		all.Include(shape[i].rect);
	WindowShape windowShape;
	windowShape.SetTo(shape, all, false, false, make_color(0, 0, 0));

	const std::vector<WindowShape::Corner>& corners = windowShape.Corners();
	for (size_t i = 0; i < corners.size(); i++) {
		const WindowShape::Corner& corner = corners[i];
		if (!windowShape.EdgeRegion().Intersects(corner.frame))
			continue;
		const uint8* coverage = WindowShape::CoverageFor(corner.radius);
		if (coverage == NULL)
			continue;

		cairo_surface_t* cut = cairo_image_surface_create(CAIRO_FORMAT_A8,
			corner.radius, corner.radius);
		cairo_surface_flush(cut);
		uint8* bits = cairo_image_surface_get_data(cut);
		const int32 stride = cairo_image_surface_get_stride(cut);
		const bool right = corner.kind == WindowShape::RIGHT_TOP
			|| corner.kind == WindowShape::RIGHT_BOTTOM;
		const bool bottom = corner.kind == WindowShape::LEFT_BOTTOM
			|| corner.kind == WindowShape::RIGHT_BOTTOM;
		for (int32 row = 0; row < corner.radius; row++) {
			for (int32 column = 0; column < corner.radius; column++) {
				const int32 x = right ? corner.radius - 1 - column : column;
				const int32 y = bottom ? corner.radius - 1 - row : row;
				bits[y * stride + x]
					= 255 - coverage[row * corner.radius + column];
			}
		}
		cairo_surface_mark_dirty(cut);

		cairo_save(cr);
		cairo_set_operator(cr, CAIRO_OPERATOR_DEST_OUT);
		cairo_set_source_rgba(cr, 0, 0, 0, 1);
		cairo_mask_surface(cr, cut, corner.frame.left, corner.frame.top);
		cairo_restore(cr);
		cairo_surface_destroy(cut);
	}
}


// #pragma mark - the frame's bitmap


static bool
prepare_bitmap(cosmoe_decor* decor, int32 width, int32 height)
{
	if (decor->bitmap != NULL
		&& decor->bitmap->Bounds().IntegerWidth() + 1 >= width
		&& decor->bitmap->Bounds().IntegerHeight() + 1 >= height) {
		return true;
	}

	decor->engine.SetTarget(NULL, NULL);
	delete decor->bitmap;
	decor->view = NULL;

	width = (width + kBitmapStep - 1) / kBitmapStep * kBitmapStep;
	height = (height + kBitmapStep - 1) / kBitmapStep * kBitmapStep;
	decor->bitmap = new(std::nothrow) BBitmap(BRect(0, 0, width - 1,
		height - 1), B_BITMAP_ACCEPTS_VIEWS, B_RGBA32);
	if (decor->bitmap == NULL || decor->bitmap->InitCheck() != B_OK) {
		delete decor->bitmap;
		decor->bitmap = NULL;
		return false;
	}

	decor->view = new(std::nothrow) BView(decor->bitmap->Bounds(), "frame",
		B_FOLLOW_NONE, 0);
	if (decor->view == NULL)
		return false;
	decor->bitmap->Lock();
	decor->bitmap->AddChild(decor->view);
	decor->bitmap->Unlock();
	return true;
}


// #pragma mark - add-ons


static DecorAddOn*
find_add_on(const char* name)
{
	if (sAddOns == NULL)
		sAddOns = new std::map<BString, DecorAddOn*>;

	std::map<BString, DecorAddOn*>::iterator found = sAddOns->find(name);
	if (found != sAddOns->end())
		return found->second;

	DecorAddOn* addOn = NULL;
	if (strcmp(name, "Default") == 0 || strcmp(name, "Haiku") == 0) {
		// Haiku's own, built in
		addOn = new(std::nothrow) DecorAddOn(-1, "Default");
	} else {
		static const directory_which kDirectories[] = {
			B_USER_NONPACKAGED_ADDONS_DIRECTORY,
			B_USER_ADDONS_DIRECTORY,
			B_SYSTEM_NONPACKAGED_ADDONS_DIRECTORY,
			B_SYSTEM_ADDONS_DIRECTORY
		};
		for (size_t i = 0; i < B_COUNT_OF(kDirectories) && addOn == NULL;
				i++) {
			BPath path;
			if (find_directory(kDirectories[i], &path) != B_OK
				|| path.Append("decorators") != B_OK
				|| path.Append(name) != B_OK) {
				continue;
			}

			image_id image = load_add_on(path.Path());
			if (image < 0)
				continue;

			instantiate_decor_addon_func instantiate;
			if (get_image_symbol(image, "instantiate_decor_addon",
					B_SYMBOL_TYPE_TEXT, (void**)&instantiate) != B_OK) {
				fprintf(stderr, "Hokku: %s is no decorator\n", path.Path());
				unload_add_on(image);
				continue;
			}
			addOn = instantiate(image, name);
			if (addOn != NULL && addOn->InitCheck() != B_OK) {
				delete addOn;
				addOn = NULL;
			}
			// add-ons stay loaded: frames made with one keep its code
		}
	}

	if (addOn != NULL)
		(*sAddOns)[name] = addOn;
	return addOn;
}


// #pragma mark - the calls


static cosmoe_decor*
decor_create(uint32_t look, uint32_t flags, const char* title)
{
	BAutolock locker(sLock);
	if (sAddOn == NULL)
		return NULL;

	cosmoe_decor* decor = new(std::nothrow) cosmoe_decor;
	if (decor == NULL)
		return NULL;

	decor->addOn = sAddOn;
	decor->bitmap = NULL;
	decor->view = NULL;
	decor->look = (window_look)look;
	decor->flags = flags;
	decor->title = title != NULL ? title : "";
	decor->focused = false;
	decor->maximized = false;
	decor->laidOut = false;
	decor->settingsGeneration = sSettingsGeneration;
	decor->content = BRect(0, 0, 199, 99);
	decor->shadow = NULL;
	decor->shadowX = decor->shadowY = 0;
	decor->shadowGlow = false;

	decor->decorator = sAddOn->AllocateDecorator(Desktop::Default(),
		&decor->engine, decor->content, title != NULL ? title : "",
		decor->look, flags);
	if (decor->decorator == NULL) {
		delete decor;
		return NULL;
	}
	return decor;
}


static void
decor_destroy(cosmoe_decor* decor)
{
	if (decor == NULL)
		return;
	decor->engine.SetTarget(NULL, NULL);
	delete decor->decorator;
	delete decor->bitmap;
	if (decor->shadow != NULL)
		cairo_surface_destroy(decor->shadow);
	delete decor;
}


static void
decor_set_title(cosmoe_decor* decor, const char* title)
{
	decor->title = title != NULL ? title : "";
	decor->decorator->SetTitle(0, decor->title.String());
}


static void
decor_set_flags(cosmoe_decor* decor, uint32_t flags)
{
	if (flags == decor->flags)
		return;
	decor->flags = flags;
	decor->decorator->SetFlags(0, flags);
}


static void
decor_set_focus(cosmoe_decor* decor, int focused)
{
	if (decor->focused == (focused != 0))
		return;
	decor->focused = focused != 0;
	decor->decorator->SetFocus(0, decor->focused);
}


static void
decor_set_maximized(cosmoe_decor* decor, int maximized)
{
	decor->maximized = maximized != 0;
}


static void
decor_borders(cosmoe_decor* decor, int* left, int* top, int* right,
	int* bottom)
{
	BRect frame = decor->decorator->GetFootprint().Frame();
	const BRect& content = decor->content;
	*left = std::max((int)(content.left - frame.left), 0);
	*top = std::max((int)(content.top - frame.top), 0);
	*right = std::max((int)(frame.right - content.right), 0);
	*bottom = std::max((int)(frame.bottom - content.bottom), 0);
}


static void
decor_shadow_extent(cosmoe_decor* decor, int* left, int* top, int* right,
	int* bottom)
{
	if (!has_shadow(decor)) {
		*left = *top = *right = *bottom = 0;
		return;
	}
	const int32 margin = shadow_margin();
	const int32 offset = is_glow() ? 0 : kShadowOffset;
	*left = margin;
	*right = margin;
	*top = std::max(margin - offset, (int32)0);
	*bottom = margin + offset;
}


static void
decor_layout(cosmoe_decor* decor, int x, int y, int width, int height)
{
	if (width < 1)
		width = 1;
	if (height < 1)
		height = 1;
	BRect content(x, y, x + width - 1, y + height - 1);
	if (content == decor->content && decor->laidOut)
		return;

	if (!decor->laidOut) {
		// Made again where it is to be, as app_server makes them: not every
		// decorator lays out its tab anew when it is resized.
		decor->laidOut = true;
		Decorator* decorator = decor->addOn->AllocateDecorator(
			Desktop::Default(), &decor->engine, content, decor->title.String(),
			decor->look, decor->flags);
		if (decorator != NULL) {
			decorator->SetFocus(0, decor->focused);
			delete decor->decorator;
			decor->decorator = decorator;
			decor->content = content;
			return;
		}
	}

	Decorator* decorator = decor->decorator;
	decorator->MoveBy(content.LeftTop() - decor->content.LeftTop());
	BRegion dirty;
	decorator->ResizeBy(BPoint(content.Width() - decor->content.Width(),
		content.Height() - decor->content.Height()), &dirty);
	decor->content = content;
}


static int
decor_region_at(cosmoe_decor* decor, int x, int y)
{
	int32 tab = -1;
	return (int)decor->decorator->RegionAt(BPoint(x, y), tab);
}


static void
decor_button_rect(cosmoe_decor* decor, int region,
	struct cosmoe_decor_rect* rect)
{
	rect->left = rect->top = 0;
	rect->right = rect->bottom = -1;

	Decorator::Tab* tab = decor->decorator->TabAt(0);
	if (tab == NULL)
		return;
	BRect button;
	switch (region) {
		case COSMOE_DECOR_REGION_CLOSE_BUTTON:
			button = tab->closeRect;
			break;
		case COSMOE_DECOR_REGION_ZOOM_BUTTON:
			button = tab->zoomRect;
			break;
		case COSMOE_DECOR_REGION_MINIMIZE_BUTTON:
			button = tab->minimizeRect;
			break;
		default:
			return;
	}
	if (!button.IsValid())
		return;
	rect->left = (int)button.left;
	rect->top = (int)button.top;
	rect->right = (int)button.right;
	rect->bottom = (int)button.bottom;
}


static void
decor_set_pressed(cosmoe_decor* decor, int region, int pressed)
{
	switch (region) {
		case COSMOE_DECOR_REGION_CLOSE_BUTTON:
			decor->decorator->SetClose(0, pressed != 0);
			break;
		case COSMOE_DECOR_REGION_ZOOM_BUTTON:
			decor->decorator->SetZoom(0, pressed != 0);
			break;
		case COSMOE_DECOR_REGION_MINIMIZE_BUTTON:
			decor->decorator->SetMinimize(0, pressed != 0);
			break;
	}
}


static void
decor_footprint(cosmoe_decor* decor,
	void (*add)(void* data, int x, int y, int width, int height), void* data)
{
	BRegion region(decor->decorator->GetFootprint());
	region.Include(decor->content);
	for (int32 i = 0; i < region.CountRects(); i++) {
		clipping_rect rect = region.RectAtInt(i);
		add(data, rect.left, rect.top, rect.right - rect.left + 1,
			rect.bottom - rect.top + 1);
	}
}


static void
decor_paint(cosmoe_decor* decor, cairo_t* cr)
{
	if (decor->settingsGeneration != sSettingsGeneration) {
		decor->settingsGeneration = sSettingsGeneration;
		DesktopSettings settings(Desktop::Default());
		BRegion dirty;
		decor->decorator->FontsChanged(settings, &dirty);
		decor->decorator->ColorsChanged(settings, &dirty);
	}

	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_restore(cr);

	// the frame, through the decorator
	BRect bounds = decor->decorator->GetFootprint().Frame();
	bounds = bounds | decor->content;
	const int32 width = (int32)bounds.right + 1;
	const int32 height = (int32)bounds.bottom + 1;
	if (width <= 0 || height <= 0 || !prepare_bitmap(decor, width, height))
		return;

	BBitmap* bitmap = decor->bitmap;
	if (!bitmap->Lock())
		return;
	cairo_surface_t* surface = BBitmap::Private(bitmap).ViewSurface();
	if (surface == NULL) {
		bitmap->Unlock();
		return;
	}
	const double scale = (double)cairo_image_surface_get_width(surface)
		/ (bitmap->Bounds().IntegerWidth() + 1);

	cairo_t* clear = cairo_create(surface);
	cairo_set_operator(clear, CAIRO_OPERATOR_CLEAR);
	cairo_rectangle(clear, 0, 0, width * scale, height * scale);
	cairo_fill(clear);
	cairo_destroy(clear);
	cairo_surface_mark_dirty(surface);

	decor->engine.SetTarget(bitmap, decor->view);
	decor->decorator->Draw(bounds);
	decor->view->Sync();
	decor->engine.SetTarget(NULL, NULL);
	cairo_surface_flush(surface);

	cairo_save(cr);
	cairo_rectangle(cr, 0, 0, width, height);
	cairo_clip(cr);
	cairo_scale(cr, 1 / scale, 1 / scale);
	cairo_set_source_surface(cr, surface, 0, 0);
	cairo_paint(cr);
	cairo_restore(cr);

	update_shadow(decor, surface, scale, bounds);
	bitmap->Unlock();

	// what the rounded corners cut off is left clear
	cut_corners(decor, cr);

	// and the shadow or glow goes under the frame, round the content
	if (decor->shadow != NULL) {
		cairo_save(cr);
		cairo_rectangle(cr, decor->shadowX, decor->shadowY,
			cairo_image_surface_get_width(decor->shadow),
			cairo_image_surface_get_height(decor->shadow));
		cairo_rectangle(cr, decor->content.left, decor->content.top,
			decor->content.Width() + 1, decor->content.Height() + 1);
		cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
		cairo_clip(cr);

		rgb_color color = decor->shadowGlow
			? ui_color(B_PANEL_TEXT_COLOR) : make_color(0, 0, 0);
		const uint8 alpha = decor->shadowGlow ? kGlowAlpha : kShadowAlpha;
		cairo_set_operator(cr, CAIRO_OPERATOR_DEST_OVER);
		cairo_set_source_rgba(cr, color.red / 255.0, color.green / 255.0,
			color.blue / 255.0, alpha / 255.0);
		cairo_mask_surface(cr, decor->shadow, decor->shadowX,
			decor->shadowY);
		cairo_restore(cr);
	}
}


static void
decor_paint_overlay(cosmoe_decor* decor, cairo_t* cr)
{
	cairo_save(cr);
	cairo_rectangle(cr, decor->content.left, decor->content.top,
		decor->content.Width() + 1, decor->content.Height() + 1);
	cairo_clip(cr);

	// What the decorator draws over the content, like a document window's
	// resize knob: the frame's bitmap is clear elsewhere there.
	BBitmap* bitmap = decor->bitmap;
	if (bitmap != NULL && bitmap->Lock()) {
		cairo_surface_t* surface = BBitmap::Private(bitmap).ViewSurface();
		if (surface != NULL) {
			const double scale = (double)cairo_image_surface_get_width(surface)
				/ (bitmap->Bounds().IntegerWidth() + 1);
			cairo_save(cr);
			cairo_scale(cr, 1 / scale, 1 / scale);
			cairo_set_source_surface(cr, surface, 0, 0);
			cairo_paint(cr);
			cairo_restore(cr);
		}
		bitmap->Unlock();
	}

	// rounded corners that reach over the content; the frame's own were
	// cut when it was painted
	cut_corners(decor, cr);
	cairo_restore(cr);
}


static void
set_rect(struct cosmoe_decor_rect* rect, BRect frame)
{
	if (!frame.IsValid()) {
		rect->left = rect->top = 0;
		rect->right = rect->bottom = -1;
		return;
	}
	rect->left = (int)frame.left;
	rect->top = (int)frame.top;
	rect->right = (int)frame.right;
	rect->bottom = (int)frame.bottom;
}


static void
decor_frame_rects(cosmoe_decor* decor, struct cosmoe_decor_rect* border,
	struct cosmoe_decor_rect* tab)
{
	set_rect(border, decor->decorator->BorderRect());
	set_rect(tab, decor->decorator->CountTabs() > 0
		? decor->decorator->TabRect(0) : BRect());
}


static const cosmoe_decor_ops kOps = {
	decor_create,
	decor_destroy,
	decor_set_title,
	decor_set_flags,
	decor_set_focus,
	decor_set_maximized,
	decor_borders,
	decor_shadow_extent,
	decor_layout,
	decor_region_at,
	decor_button_rect,
	decor_set_pressed,
	decor_footprint,
	decor_paint,
	decor_paint_overlay,
	decor_frame_rects
};


extern "C" const struct cosmoe_decor_ops*
cosmoe_decor_load(const char* name)
{
	BAutolock locker(sLock);
	DecorAddOn* addOn = find_add_on(name);
	if (addOn == NULL)
		return NULL;
	sAddOn = addOn;
	sAddOnName = name;
	return &kOps;
}


extern "C" int
cosmoe_decor_init_standalone(void)
{
	static bool sInitialized = false;
	BAutolock locker(sLock);
	if (sInitialized || be_app != NULL)
		return 0;
	if (_init_interface_kit_() != B_OK)
		return -1;
	sInitialized = true;
	return 0;
}


extern "C" void
cosmoe_decor_reload_settings(void)
{
	BAutolock locker(sLock);
	BPrivate::reload_appearance();
	sSettingsGeneration++;
}


extern "C" void
cosmoe_decor_current(char* name, size_t size)
{
	BString current;
	BPrivate::get_decorator(current);
	if (current.IsEmpty())
		current = "Default";
	snprintf(name, size, "%s", current.String());
}


extern "C" void
cosmoe_decor_settings_folder(char* path, size_t size)
{
	BPath folder;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &folder) != B_OK
		|| folder.Append("system/app_server") != B_OK) {
		snprintf(path, size, "%s", "");
		return;
	}
	snprintf(path, size, "%s", folder.Path());
}
