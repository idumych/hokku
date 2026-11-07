/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Hokku: the app_server classes decorators use besides the drawing engine,
 * on top of libbe.
 */


#include <new>
#include <string.h>

#include <cairo.h>

#include <DecoratorPrivate.h>
#include <Dragger.h>
#include <InterfaceDefs.h>
#include <Menu.h>
#include <ScrollBar.h>

#include "DecorManager.h"
#include "DefaultDecorator.h"
#include "Desktop.h"
#include "DesktopSettings.h"
#include "DrawState.h"
#include "PatternHandler.h"
#include "ServerBitmap.h"
#include "ServerFont.h"


const Pattern kSolidHigh(0xFFFFFFFFFFFFFFFFLL);
const Pattern kSolidLow((uint64)0);
const Pattern kMixedColors(0xAAAAAAAAAAAAAAAALL);


// #pragma mark - ServerFont


float
ServerFont::StringWidth(const char* string, int32 numBytes,
	const escapement_delta* delta) const
{
	if (string == NULL || numBytes <= 0)
		return 0.0f;
	return fFont.StringWidth(string, numBytes);
}


const char*
ServerFont::Family() const
{
	fFont.GetFamilyAndStyle(&fFamily, &fStyle);
	return fFamily;
}


const char*
ServerFont::Style() const
{
	fFont.GetFamilyAndStyle(&fFamily, &fStyle);
	return fStyle;
}


// #pragma mark - DrawState


DrawState::DrawState()
	:
	fOrigin(0, 0),
	fScale(1.0f),
	fHighColor(make_color(0, 0, 0)),
	fLowColor(make_color(255, 255, 255)),
	fPattern(kSolidHigh),
	fDrawingMode(B_OP_COPY),
	fAlphaSourceMode(B_PIXEL_ALPHA),
	fAlphaFunctionMode(B_ALPHA_OVERLAY),
	fPenSize(1.0f)
{
}


DrawState::DrawState(const DrawState& other)
	:
	fOrigin(other.fOrigin),
	fScale(other.fScale),
	fHighColor(other.fHighColor),
	fLowColor(other.fLowColor),
	fPattern(other.fPattern),
	fDrawingMode(other.fDrawingMode),
	fAlphaSourceMode(other.fAlphaSourceMode),
	fAlphaFunctionMode(other.fAlphaFunctionMode),
	fPenSize(other.fPenSize),
	fFont(other.fFont)
{
}


DrawState::~DrawState()
{
}


void
DrawState::SetFont(const ServerFont& font, uint32 flags)
{
	if (flags == B_FONT_ALL) {
		fFont = font;
		return;
	}

	BFont mixed(fFont.GetBFont());
	const BFont& from = font.GetBFont();
	if ((flags & B_FONT_FAMILY_AND_STYLE) != 0) {
		font_family family;
		font_style style;
		from.GetFamilyAndStyle(&family, &style);
		mixed.SetFamilyAndStyle(family, style);
	}
	if ((flags & B_FONT_SIZE) != 0)
		mixed.SetSize(from.Size());
	if ((flags & B_FONT_SHEAR) != 0)
		mixed.SetShear(from.Shear());
	if ((flags & B_FONT_ROTATION) != 0)
		mixed.SetRotation(from.Rotation());
	if ((flags & B_FONT_SPACING) != 0)
		mixed.SetSpacing(from.Spacing());
	if ((flags & B_FONT_ENCODING) != 0)
		mixed.SetEncoding(from.Encoding());
	if ((flags & B_FONT_FACE) != 0)
		mixed.SetFace(from.Face());
	if ((flags & B_FONT_FLAGS) != 0)
		mixed.SetFlags(from.Flags());
	if ((flags & B_FONT_FALSE_BOLD_WIDTH) != 0)
		mixed.SetFalseBoldWidth(from.FalseBoldWidth());
	fFont = ServerFont(mixed);
}


// #pragma mark - ServerBitmap


ServerBitmap::ServerBitmap(cairo_surface_t* surface)
	:
	fSurface(surface),
	fWidth(cairo_image_surface_get_width(surface)),
	fHeight(cairo_image_surface_get_height(surface))
{
}


ServerBitmap::~ServerBitmap()
{
	cairo_surface_destroy(fSurface);
}


uint8*
ServerBitmap::Bits() const
{
	cairo_surface_flush(fSurface);
	return cairo_image_surface_get_data(fSurface);
}


uint32
ServerBitmap::BytesPerRow() const
{
	return cairo_image_surface_get_stride(fSurface);
}


// #pragma mark - Desktop


Desktop::Desktop()
{
}


Desktop::~Desktop()
{
}


/*static*/ Desktop*
Desktop::Default()
{
	static Desktop sDesktop;
	return &sDesktop;
}


void
Desktop::ReloadDecor()
{
	// New windows take the decorator's settings; there is nothing kept
	// between them to reload.
}


// #pragma mark - DesktopSettings


DesktopSettings::DesktopSettings(Desktop* desktop)
{
}


void
DesktopSettings::GetDefaultPlainFont(ServerFont& font) const
{
	font = ServerFont(*be_plain_font);
}


void
DesktopSettings::GetDefaultBoldFont(ServerFont& font) const
{
	font = ServerFont(*be_bold_font);
}


void
DesktopSettings::GetDefaultFixedFont(ServerFont& font) const
{
	font = ServerFont(*be_fixed_font);
}


void
DesktopSettings::GetScrollBarInfo(scroll_bar_info& info) const
{
	get_scroll_bar_info(&info);
}


void
DesktopSettings::GetMenuInfo(menu_info& info) const
{
	get_menu_info(&info);
}


mode_mouse
DesktopSettings::MouseMode() const
{
	return mouse_mode();
}


mode_focus_follows_mouse
DesktopSettings::FocusFollowsMouseMode() const
{
	// the Linux desktop decides; libbe has no setting for it
	return B_NORMAL_FOCUS_FOLLOWS_MOUSE;
}


bool
DesktopSettings::AcceptFirstClick() const
{
	return accept_first_click();
}


bool
DesktopSettings::ShowAllDraggers() const
{
	return BDragger::AreDraggersDrawn();
}


int32
DesktopSettings::WorkspacesCount() const
{
	return count_workspaces();
}


int32
DesktopSettings::WorkspacesColumns() const
{
	return count_workspaces();
}


int32
DesktopSettings::WorkspacesRows() const
{
	return 1;
}


rgb_color
DesktopSettings::UIColor(color_which which) const
{
	return ui_color(which);
}


bool
DesktopSettings::CompositedShadows() const
{
	return BPrivate::get_window_shadows();
}


bool
DesktopSettings::CompositedRoundedCorners() const
{
	return BPrivate::get_window_rounded_corners();
}


const BString&
DesktopSettings::ControlLook() const
{
	DesktopSettings* self = const_cast<DesktopSettings*>(this);
	self->fControlLook = "";
	return fControlLook;
}


// #pragma mark - DecorAddOn


DecorAddOn::DecorAddOn(image_id id, const char* name)
	:
	fImageID(id),
	fName(name)
{
}


DecorAddOn::~DecorAddOn()
{
}


status_t
DecorAddOn::InitCheck() const
{
	return B_OK;
}


Decorator*
DecorAddOn::AllocateDecorator(Desktop* desktop, DrawingEngine* engine,
	BRect rect, const char* title, window_look look, uint32 flags)
{
	DesktopSettings settings(desktop);
	Decorator* decorator = _AllocateDecorator(settings, rect, desktop);
	if (decorator == NULL)
		return NULL;

	decorator->UpdateColors(settings);

	if (decorator->AddTab(settings, title, look, flags) == NULL) {
		delete decorator;
		return NULL;
	}

	decorator->SetDrawingEngine(engine);
	return decorator;
}


WindowBehaviour*
DecorAddOn::AllocateWindowBehaviour(Window* window)
{
	return NULL;
}


const DesktopListenerList&
DecorAddOn::GetDesktopListeners()
{
	return fDesktopListeners;
}


Decorator*
DecorAddOn::_AllocateDecorator(DesktopSettings& settings, BRect rect,
	Desktop* desktop)
{
	return new(std::nothrow) DefaultDecorator(settings, rect, desktop);
}
