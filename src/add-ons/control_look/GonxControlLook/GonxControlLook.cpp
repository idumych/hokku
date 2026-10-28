/*
 * Copyright 2026, Gonx contributors.
 * Distributed under the terms of the MIT License.
 *
 * Control look after the Gonx BeOS concept. Colours were measured from the
 * concept screenshots (cotito.free.fr, c. 2001); see GonxControlLook.h.
 */


#include "GonxControlLook.h"

#include <algorithm>
#include <cmath>
#include <new>

#include <Control.h>
#include <GradientLinear.h>
#include <InterfaceDefs.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <MenuItem.h>

#include <MenuPrivate.h>
#include <Rect.h>
#include <TabView.h>
#include <View.h>


namespace BPrivate {


static const rgb_color kWhite = { 255, 255, 255, 255 };
static const rgb_color kBlack = { 0, 0, 0, 255 };

// control borders and drop shadows
static const rgb_color kBorder = { 150, 149, 142, 255 };
static const rgb_color kBorderPressed = { 111, 110, 104, 255 };

// the default button's blue frame and the glow under it
static const rgb_color kDefaultDark = { 94, 108, 138, 255 };
static const rgb_color kDefaultMid = { 137, 149, 176, 255 };
static const rgb_color kDefaultLight = { 214, 224, 238, 255 };
static const rgb_color kDefaultShadowNear = { 110, 135, 155, 255 };
static const rgb_color kDefaultShadowFar = { 178, 194, 204, 255 };

// check marks, arrows, the slider's position marker
static const rgb_color kMark = { 59, 82, 118, 255 };
static const rgb_color kArrow = { 126, 125, 119, 255 };
static const rgb_color kMarker = { 245, 196, 0, 255 };
static const rgb_color kMarkerOutline = { 110, 90, 0, 255 };

// Dano's shapes in the concept's colours: an outline with cut corners and a
// soft drop shadow, two pixels thick all round (warm grey, or blue-grey on a
// default button), flat faces, and a coloured box holding a pop-up's arrow
static const rgb_color kOutline = { 148, 147, 141, 255 };
static const rgb_color kOutlinePressed = { 110, 109, 103, 255 };
static const rgb_color kDefaultOutline = { 109, 118, 147, 255 };
static const rgb_color kDefaultGlow = { 143, 163, 184, 255 };
static const rgb_color kIndicator = { 104, 128, 168, 255 };
static const float kButtonRadius = 4.0f;

static const float kTabRadius = 5.0f;
static const float kGroupRadius = 5.0f;


static rgb_color
mix(rgb_color a, rgb_color b, float amount)
{
	rgb_color result;
	result.red = (uint8)roundf(a.red + (b.red - a.red) * amount);
	result.green = (uint8)roundf(a.green + (b.green - a.green) * amount);
	result.blue = (uint8)roundf(a.blue + (b.blue - a.blue) * amount);
	result.alpha = a.alpha;
	return result;
}


static rgb_color
border_color(const rgb_color& background, uint32 flags)
{
	if ((flags & BControlLook::B_DISABLED) != 0)
		return mix(kOutline, background, 0.5f);
	if ((flags & (BControlLook::B_ACTIVATED | BControlLook::B_CLICKED)) != 0)
		return kOutlinePressed;
	return kOutline;
}


/*!	A button's outline: blue-grey on the default button, warm grey on the
	others, as in the concept.
*/
static rgb_color
button_outline(const rgb_color& background, uint32 flags)
{
	if ((flags & BControlLook::B_DEFAULT_BUTTON) == 0)
		return border_color(background, flags);
	if ((flags & BControlLook::B_DISABLED) != 0)
		return mix(kDefaultOutline, background, 0.5f);
	return kDefaultOutline;
}


static rgb_color
shadow_color(const rgb_color& background)
{
	return mix(background, kBlack, 0.2f);
}


/*!	The concept's buttons and fields have a warm off-white face (#f9f8f4
	to #f4f5ef in its screenshots) whatever the control colour, so it is
	used as is; \a base only matters when the face is pressed or disabled.
*/
static rgb_color
face_color(const rgb_color& base, uint32 flags, float lightness)
{
	static const rgb_color kFace = { 247, 247, 242, 255 };

	if ((flags & BControlLook::B_DISABLED) != 0)
		return mix(kFace, base, 0.5f);
	if ((flags & (BControlLook::B_ACTIVATED
			| BControlLook::B_PARTIALLY_ACTIVATED)) != 0)
		return mix(base, kBlack, 0.12f);
	if ((flags & BControlLook::B_HOVER) != 0)
		return mix(kFace, kDefaultLight, 0.5f);
	return kFace;
}


/*!	How far row \a row (0 = the edge) of a corner with radius \a radius is
	pushed in.
*/
static float
corner_inset(float radius, int32 row)
{
	if (row >= radius)
		return 0;

	const float distance = radius - row - 0.5f;
	return floorf(radius
		- sqrtf(std::max(radius * radius - distance * distance, 0.0f)) + 0.5f);
}


/*!	A rounded rectangle set row by row, so its corners come out as crisp
	pixel steps like Dano's rather than anti-aliased.
*/
static void
fill_rounded(BView* view, BRect rect, float radius, const rgb_color& color)
{
	if (!rect.IsValid())
		return;

	view->SetHighColor(color);
	const int32 rows = (int32)std::min(radius, floorf((rect.Height() + 1) / 2));
	for (int32 row = 0; row < rows; row++) {
		const float inset = corner_inset(radius, row);
		view->FillRect(BRect(rect.left + inset, rect.top + row,
			rect.right - inset, rect.top + row));
		view->FillRect(BRect(rect.left + inset, rect.bottom - row,
			rect.right - inset, rect.bottom - row));
	}
	if (rect.top + rows <= rect.bottom - rows) {
		view->FillRect(BRect(rect.left, rect.top + rows, rect.right,
			rect.bottom - rows));
	}
}


static BRect
extend_past_open_sides(BRect rect, uint32 borders, float amount)
{
	if ((borders & BControlLook::B_LEFT_BORDER) == 0)
		rect.left -= amount;
	if ((borders & BControlLook::B_TOP_BORDER) == 0)
		rect.top -= amount;
	if ((borders & BControlLook::B_RIGHT_BORDER) == 0)
		rect.right += amount;
	if ((borders & BControlLook::B_BOTTOM_BORDER) == 0)
		rect.bottom += amount;
	return rect;
}


static void
inset_by_borders(BRect& rect, float inset, uint32 borders)
{
	if ((borders & BControlLook::B_LEFT_BORDER) != 0)
		rect.left += inset;
	if ((borders & BControlLook::B_TOP_BORDER) != 0)
		rect.top += inset;
	if ((borders & BControlLook::B_RIGHT_BORDER) != 0)
		rect.right -= inset;
	if ((borders & BControlLook::B_BOTTOM_BORDER) != 0)
		rect.bottom -= inset;
}


static bool
is_flat(uint32 flags)
{
	return (flags & BControlLook::B_FLAT) != 0
		&& (flags & (BControlLook::B_ACTIVATED
			| BControlLook::B_PARTIALLY_ACTIVATED)) == 0
		&& ((flags & (BControlLook::B_HOVER | BControlLook::B_FOCUSED)) == 0
			|| (flags & BControlLook::B_DISABLED) != 0);
}


//	#pragma mark -


GonxControlLook::GonxControlLook()
{
}


GonxControlLook::~GonxControlLook()
{
}


//	#pragma mark - buttons


void
GonxControlLook::DrawButtonFrame(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base,
	const rgb_color& background, uint32 flags, uint32 borders)
{
	_DrawGonxButtonFrame(view, rect, updateRect, base, background, flags,
		borders, true);
}


void
GonxControlLook::DrawButtonFrame(BView* view, BRect& rect,
	const BRect& updateRect, float, const rgb_color& base,
	const rgb_color& background, uint32 flags, uint32 borders)
{
	_DrawGonxButtonFrame(view, rect, updateRect, base, background, flags,
		borders, true);
}


void
GonxControlLook::DrawButtonFrame(BView* view, BRect& rect,
	const BRect& updateRect, float, float, float, float,
	const rgb_color& base, const rgb_color& background, uint32 flags,
	uint32 borders)
{
	_DrawGonxButtonFrame(view, rect, updateRect, base, background, flags,
		borders, true);
}


void
GonxControlLook::DrawButtonBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders, orientation orientation)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;
	_DrawFace(view, rect, base, flags, borders, orientation);
}


void
GonxControlLook::DrawButtonBackground(BView* view, BRect& rect,
	const BRect& updateRect, float, const rgb_color& base, uint32 flags,
	uint32 borders, orientation orientation)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;
	_DrawFace(view, rect, base, flags, borders, orientation);
}


void
GonxControlLook::DrawButtonBackground(BView* view, BRect& rect,
	const BRect& updateRect, float, float, float, float,
	const rgb_color& base, uint32 flags, uint32 borders,
	orientation orientation)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;
	_DrawFace(view, rect, base, flags, borders, orientation);
}


void
GonxControlLook::DrawButtonWithPopUpBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders, orientation orientation)
{
	DrawButtonWithPopUpBackground(view, rect, updateRect, 0, 0, 0, 0, base,
		flags, borders, orientation);
}


void
GonxControlLook::DrawButtonWithPopUpBackground(BView* view, BRect& rect,
	const BRect& updateRect, float, const rgb_color& base, uint32 flags,
	uint32 borders, orientation orientation)
{
	DrawButtonWithPopUpBackground(view, rect, updateRect, 0, 0, 0, 0, base,
		flags, borders, orientation);
}


void
GonxControlLook::DrawButtonWithPopUpBackground(BView* view, BRect& rect,
	const BRect& updateRect, float, float, float, float,
	const rgb_color& base, uint32 flags, uint32 borders,
	orientation orientation)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	const BRect face(rect);
	_DrawFace(view, rect, base, flags, borders, orientation);

	// same split as Haiku's look, so labels stay where they were
	rect.right -= ComposeSpacing(B_USE_ITEM_SPACING);
	_DrawIndicator(view, BRect(rect.right + 1, face.top, face.right,
		face.bottom), face, base, flags, B_DOWN_ARROW);
}


//	#pragma mark - menu fields and menus


void
GonxControlLook::DrawMenuFieldFrame(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base,
	const rgb_color& background, uint32 flags, uint32 borders)
{
	// The box is centred on the menu bar inside, at the concept's size;
	// the field draws the part of it outside the bar, the bar the rest (see
	// DrawMenuFieldBackground()).
	if (!ShouldDraw(view, rect, updateRect))
		return;
	if ((flags & B_BLEND_FRAME) == 0) {
		view->PushState();
		view->SetHighColor(background);
		view->FillRect(rect);
		view->PopState();
	}

	BMenuField* field = dynamic_cast<BMenuField*>(view);
	BMenuBar* menuBar = field != NULL ? field->MenuBar() : NULL;
	if (menuBar != NULL) {
		// BMenuField draws this frame two pixels around its menu bar
		const BRect bar = rect.InsetByCopy(2, 2);
		_DrawMenuFieldBox(view, menuBar, bar, base, background, flags);
	}

	inset_by_borders(rect, 2, borders);
}


void
GonxControlLook::DrawMenuFieldFrame(BView* view, BRect& rect,
	const BRect& updateRect, float, const rgb_color& base,
	const rgb_color& background, uint32 flags, uint32 borders)
{
	DrawMenuFieldFrame(view, rect, updateRect, base, background, flags,
		borders);
}


void
GonxControlLook::DrawMenuFieldFrame(BView* view, BRect& rect,
	const BRect& updateRect, float, float, float, float,
	const rgb_color& base, const rgb_color& background, uint32 flags,
	uint32 borders)
{
	DrawMenuFieldFrame(view, rect, updateRect, base, background, flags,
		borders);
}


void
GonxControlLook::DrawMenuFieldBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, bool popupIndicator,
	uint32 flags)
{
	DrawMenuFieldBackground(view, rect, updateRect, 0, 0, 0, 0, base,
		popupIndicator, flags);
}


void
GonxControlLook::DrawMenuFieldBackground(BView* view, BRect& rect,
	const BRect& updateRect, float, const rgb_color& base,
	bool popupIndicator, uint32 flags)
{
	DrawMenuFieldBackground(view, rect, updateRect, 0, 0, 0, 0, base,
		popupIndicator, flags);
}


void
GonxControlLook::DrawMenuFieldBackground(BView* view, BRect& rect,
	const BRect& updateRect, float, float, float, float,
	const rgb_color& base, bool popupIndicator, uint32 flags)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	// The concept puts the arrow on the left. Haiku's menu field reserves
	// that room on the right, as item margins; swap them so the text moves
	// over. A menu item reads its margins as it draws, and their sum, so
	// the field's size, is unchanged.
	BMenuBar* menuBar = dynamic_cast<BMenuBar*>(view);
	if (menuBar != NULL && popupIndicator) {
		BPrivate::MenuPrivate menu(menuBar);
		float left, top, right, bottom;
		menu.GetItemMargins(&left, &top, &right, &bottom);
		if (right > left)
			menu.SetItemMargins(right, top, left, bottom);
	}

	rgb_color background = ui_color(B_PANEL_BACKGROUND_COLOR);
	if (view->Parent() != NULL
		&& view->Parent()->ViewColor() != B_TRANSPARENT_COLOR)
		background = view->Parent()->ViewColor();

	view->PushState();
	view->SetHighColor(background);
	view->FillRect(rect);
	view->PopState();

	if (menuBar == NULL) {
		_DrawFace(view, rect, base, flags & ~B_ACTIVATED, B_ALL_BORDERS,
			B_HORIZONTAL, 0.3f, false);
		return;
	}

	const BRect face = _DrawMenuFieldBox(view, menuBar, rect, base,
		background, flags);
	rect = face;
}


/*!	The concept's menu field: a square box with a soft shadow, 19 pixels
	high (at least), ending a little past the item's text, with the arrow in
	a grey segment on the left. Haiku draws a menu field's frame in the field
	around its menu bar and its background in the bar; both call this with
	the bar's rect in their own coordinates, and each draws the part that
	falls in its view. Returns the face, right of the arrow segment.
*/
BRect
GonxControlLook::_DrawMenuFieldBox(BView* view, BMenuBar* menuBar,
	BRect bar, const rgb_color& base, const rgb_color& background,
	uint32 flags)
{
	// where the item's text ends, and how much room the arrow has: the
	// difference between the item margins
	float right = bar.right;
	if (BMenuItem* item = menuBar->ItemAt(0)) {
		right = std::min(bar.right,
			bar.left + floorf(item->Frame().right) + 3);
	}
	BPrivate::MenuPrivate menu(menuBar);
	float left, top, padRight, bottom;
	menu.GetItemMargins(&left, &top, &padRight, &bottom);
	const float arrowWidth = fabsf(left - padRight);
	const bool arrow = arrowWidth > 0;

	font_height fontHeight;
	menuBar->GetFontHeight(&fontHeight);
	const float height = std::max(
		ceilf(fontHeight.ascent + fontHeight.descent) + 2, 19.0f);
	const float middle = floorf((bar.top + bar.bottom + 1) / 2);
	BRect box(bar.left, middle - floorf(height / 2), right - 1,
		middle - floorf(height / 2) + height - 1);

	view->PushState();
	if ((flags & B_DISABLED) == 0) {
		view->SetHighColor(shadow_color(background));
		view->FillRect(box.OffsetByCopy(1, 1));
	}
	view->SetHighColor(button_outline(background, flags & ~B_ACTIVATED));
	view->FillRect(box);
	view->PopState();

	BRect face = box.InsetByCopy(1, 1);
	const BRect faceRect(face);
	_DrawFace(view, face, base, flags & ~B_ACTIVATED, B_ALL_BORDERS,
		B_HORIZONTAL, 0.3f, false);

	if (arrow) {
		BRect segment(faceRect.left, faceRect.top,
			faceRect.left + arrowWidth, faceRect.bottom);
		_DrawIndicator(view, segment, faceRect, base, flags, B_RIGHT_ARROW,
			false);
		face.left = segment.right + 1;
	}
	return face;
}


void
GonxControlLook::DrawMenuFieldBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;
	_DrawFace(view, rect, base, flags, borders, B_HORIZONTAL);
}


void
GonxControlLook::DrawMenuBarBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	// flat, with a hairline where it meets the window
	const rgb_color light = mix(base, kWhite, 0.6f);
	const rgb_color line = mix(base, kBlack, 0.12f);
	_DrawFrame(view, rect, light, light, base, line, borders);

	// menus draw their items in the view's colours afterwards: leave them
	view->PushState();
	view->SetHighColor(base);
	view->FillRect(rect);
	view->PopState();
}


void
GonxControlLook::DrawMenuBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	const rgb_color border = mix(base, kBlack, 0.3f);
	_DrawFrame(view, rect, border, border, border, border, borders);

	view->PushState();
	view->SetHighColor(base);
	view->FillRect(rect);
	view->PopState();
}


void
GonxControlLook::DrawMenuItemBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	// a flat bar in the selection colour with a slightly darker edge
	const rgb_color edge = mix(base, kBlack, 0.15f);
	_DrawFrame(view, rect, edge, edge, edge, edge, borders);

	// BMenuItem draws its label in the view's high colour right after this,
	// so it must be left as it was
	view->PushState();
	view->SetHighColor(base);
	view->FillRect(rect);
	view->PopState();
}


//	#pragma mark - text, check boxes, radio buttons, groups


void
GonxControlLook::DrawTextControlBorder(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	const bool blend = (flags & B_BLEND_FRAME) != 0;
	const drawing_mode oldMode = view->DrawingMode();
	if (blend)
		view->SetDrawingMode(B_OP_ALPHA);

	// outside: the drop shadow, bottom and right
	rgb_color background = base;
	rgb_color shadow = shadow_color(base);
	if (blend) {
		background.alpha = 0;
		shadow = (rgb_color){ 0, 0, 0, 45 };
	}
	if ((flags & B_DISABLED) != 0)
		shadow = background;
	_DrawFrame(view, rect, background, background, shadow, shadow, borders);

	// the border itself, a little softer than a button's
	rgb_color border = mix(border_color(base, flags), base, 0.25f);
	if ((flags & B_DISABLED) == 0) {
		if ((flags & B_FOCUSED) != 0)
			border = ui_color(B_KEYBOARD_NAVIGATION_COLOR);
		if ((flags & B_INVALID) != 0)
			border = ui_color(B_FAILURE_COLOR);
	}
	_DrawFrame(view, rect, border, border, border, border, borders);

	view->SetDrawingMode(oldMode);
}


void
GonxControlLook::DrawCheckBox(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	const bool disabled = (flags & B_DISABLED) != 0;

	const rgb_color shadow = disabled ? base : shadow_color(base);
	_DrawFrame(view, rect, base, base, shadow, shadow);

	rgb_color border = border_color(base, flags & ~B_ACTIVATED);
	if (!disabled && (flags & B_FOCUSED) != 0)
		border = ui_color(B_KEYBOARD_NAVIGATION_COLOR);
	_DrawFrame(view, rect, border, border, border, border);

	rgb_color fill = kWhite;
	if (disabled)
		fill = mix(kWhite, base, 0.5f);
	else if ((flags & B_CLICKED) != 0)
		fill = mix(kWhite, base, 0.35f);
	view->SetHighColor(fill);
	view->FillRect(rect);

	if ((flags & (B_ACTIVATED | B_PARTIALLY_ACTIVATED)) == 0)
		return;

	// the concept's blue cross
	view->PushState();
	const uint32 viewFlags = view->Flags();
	view->SetFlags(viewFlags | B_SUBPIXEL_PRECISE);

	view->SetHighColor(disabled ? mix(kMark, base, 0.55f) : kMark);
	const float inset = std::max(2.0f, roundf((rect.Width() + 1) / 5));
	BRect mark = rect.InsetByCopy(inset, inset);
	mark.right += 1;
	mark.bottom += 1;
	view->SetPenSize(std::max(1.5f, (rect.Width() + 1) / 6.5f));
	view->SetLineMode(B_ROUND_CAP, B_ROUND_JOIN);

	if ((flags & B_PARTIALLY_ACTIVATED) != 0) {
		const float y = (mark.top + mark.bottom) / 2;
		view->StrokeLine(BPoint(mark.left, y), BPoint(mark.right, y));
	} else {
		view->StrokeLine(mark.LeftTop(), mark.RightBottom());
		view->StrokeLine(mark.LeftBottom(), mark.RightTop());
	}

	view->SetFlags(viewFlags);
	view->PopState();
}


void
GonxControlLook::DrawRadioButton(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	const bool disabled = (flags & B_DISABLED) != 0;

	BRect circle(rect);
	circle.right -= 1;
	circle.bottom -= 1;

	if (!disabled) {
		view->SetHighColor(shadow_color(base));
		view->FillEllipse(circle.OffsetByCopy(1, 1));
	}

	rgb_color border = border_color(base, flags & ~B_ACTIVATED);
	if (!disabled && (flags & B_FOCUSED) != 0)
		border = ui_color(B_KEYBOARD_NAVIGATION_COLOR);
	view->SetHighColor(border);
	view->FillEllipse(circle);

	circle.InsetBy(1, 1);
	view->SetHighColor(disabled ? mix(kWhite, base, 0.5f) : kWhite);
	view->FillEllipse(circle);

	rect = circle;

	if ((flags & (B_ACTIVATED | B_PARTIALLY_ACTIVATED)) == 0)
		return;

	BFont font;
	view->GetFont(&font);
	const float inset = roundf(font.Size() / 4);
	view->SetHighColor(disabled ? mix(kMark, base, 0.55f) : kMark);
	view->FillEllipse(circle.InsetByCopy(inset - 1, inset - 1));
}


void
GonxControlLook::DrawGroupFrame(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 borders)
{
	if (borders != B_ALL_BORDERS) {
		HaikuControlLook::DrawGroupFrame(view, rect, updateRect, base,
			borders);
		return;
	}

	// one grey line with crisp rounded corners, as in Dano
	view->PushState();
	fill_rounded(view, rect, kGroupRadius, mix(base, kBlack, 0.3f));
	fill_rounded(view, rect.InsetByCopy(1, 1), kGroupRadius - 1, base);
	view->PopState();

	rect.InsetBy(3, 3);
}


//	#pragma mark - scroll bars


void
GonxControlLook::DrawScrollBarButton(BView* view, BRect rect,
	const BRect& updateRect, const rgb_color& base, const rgb_color& text,
	uint32 flags, int32 direction, orientation orientation, bool down)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	view->PushState();
	view->ClipToRect(rect);

	// no button face, just the arrow on the track
	const rgb_color track = mix(base, kWhite, 0.3f);
	view->SetHighColor(down ? mix(track, kBlack, 0.1f) : track);
	view->FillRect(rect);

	rgb_color outline = down ? kBorderPressed : kArrow;
	if ((flags & B_DISABLED) != 0)
		outline = mix(kArrow, track, 0.55f);
	_DrawTriangle(view, rect, direction, outline, kWhite);

	view->PopState();
}


void
GonxControlLook::DrawScrollBarBackground(BView* view, BRect& rect1,
	BRect& rect2, const BRect& updateRect, const rgb_color& base,
	uint32 flags, orientation orientation)
{
	DrawScrollBarBackground(view, rect1, updateRect, base, flags,
		orientation);
	DrawScrollBarBackground(view, rect2, updateRect, base, flags,
		orientation);
}


void
GonxControlLook::DrawScrollBarBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	orientation orientation)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	view->PushState();
	view->ClipToRect(rect);
	view->SetDrawingMode(B_OP_COPY);

	const rgb_color track = mix(base, kWhite, 0.3f);
	const rgb_color edge = mix(base, kBlack, 0.18f);
	const rgb_color softEdge = mix(base, kBlack, 0.08f);
	const rgb_color shade = mix(track, kBlack, 0.04f);

	// the same edge lines as Haiku's look, so the thumb lands where it did
	if (orientation == B_HORIZONTAL) {
		if (rect.Width() > 0) {
			view->SetHighColor(edge);
			view->StrokeLine(rect.LeftTop(), rect.LeftBottom());
			rect.left++;
		}
		if (rect.Width() >= 0) {
			view->SetHighColor(softEdge);
			view->StrokeLine(rect.RightTop(), rect.RightBottom());
			rect.right--;
		}
		if (rect.Width() >= 0) {
			view->SetHighColor(shade);
			view->StrokeLine(rect.LeftTop(), rect.LeftBottom());
			rect.left++;
		}
	} else {
		if (rect.Height() > 0) {
			view->SetHighColor(edge);
			view->StrokeLine(rect.LeftTop(), rect.RightTop());
			rect.top++;
		}
		if (rect.Height() >= 0) {
			view->SetHighColor(softEdge);
			view->StrokeLine(rect.LeftBottom(), rect.RightBottom());
			rect.bottom--;
		}
		if (rect.Height() >= 0) {
			view->SetHighColor(shade);
			view->StrokeLine(rect.LeftTop(), rect.RightTop());
			rect.top++;
		}
	}

	if (rect.IsValid()) {
		view->SetHighColor(track);
		view->FillRect(rect);
	}

	view->PopState();
}


void
GonxControlLook::DrawScrollBarThumb(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	orientation orientation, uint32 knobStyle)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	view->PushState();
	view->ClipToRect(rect);

	const bool disabled = (flags & B_DISABLED) != 0;
	const rgb_color track = mix(base, kWhite, 0.3f);

	view->SetHighColor(track);
	view->FillRect(rect);

	// a pill, rounded across the bar
	BRect pill(rect);
	pill.InsetBy(1, 1);
	if (!pill.IsValid() || disabled) {
		view->PopState();
		return;
	}
	const float radius = floorf((orientation == B_HORIZONTAL
		? pill.Height() : pill.Width()) / 2);

	fill_rounded(view, pill, radius, mix(base, kBlack, 0.45f));
	fill_rounded(view, pill.InsetByCopy(1, 1), radius - 1,
		mix(base, kWhite, 0.6f));

	if (knobStyle != B_KNOB_NONE) {
		// three short grip lines in the middle
		view->SetHighColor(mix(kBorder, kWhite, 0.25f));
		const BPoint center((pill.left + pill.right) / 2,
			(pill.top + pill.bottom) / 2);
		const float length = (orientation == B_HORIZONTAL
			? pill.Height() : pill.Width()) / 4;
		if ((orientation == B_HORIZONTAL ? pill.Width() : pill.Height())
				> 6 * radius) {
			for (int32 i = -1; i <= 1; i++) {
				if (orientation == B_HORIZONTAL) {
					const float x = floorf(center.x + i * 3);
					view->StrokeLine(BPoint(x, center.y - length),
						BPoint(x, center.y + length));
				} else {
					const float y = floorf(center.y + i * 3);
					view->StrokeLine(BPoint(center.x - length, y),
						BPoint(center.x + length, y));
				}
			}
		}
	}

	view->PopState();
}


void
GonxControlLook::DrawArrowShape(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 direction,
	uint32 flags, float tint)
{
	if (direction > B_DOWN_ARROW) {
		HaikuControlLook::DrawArrowShape(view, rect, updateRect, base,
			direction, flags, tint);
		return;
	}
	if (!ShouldDraw(view, rect, updateRect))
		return;

	rgb_color outline = tint_color(base, tint);
	if ((flags & B_DISABLED) != 0)
		outline = mix(outline, base, 0.6f);
	_DrawTriangle(view, rect, direction, outline, kWhite);
}


//	#pragma mark - sliders


rgb_color
GonxControlLook::SliderBarColor(const rgb_color& base)
{
	return mix(base, kBlack, 0.06f);
}


void
GonxControlLook::DrawSliderBar(BView* view, BRect rect,
	const BRect& updateRect, const rgb_color& base, rgb_color leftFillColor,
	rgb_color rightFillColor, float sliderScale, uint32 flags,
	orientation orientation)
{
	// splits the bar and draws each side through the one-colour version
	HaikuControlLook::DrawSliderBar(view, rect, updateRect, base,
		leftFillColor, rightFillColor, sliderScale, flags, orientation);
}


void
GonxControlLook::DrawSliderBar(BView* view, BRect rect,
	const BRect& updateRect, const rgb_color& base, rgb_color fillColor,
	uint32 flags, orientation orientation)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	view->PushState();

	if ((flags & B_DISABLED) != 0)
		fillColor = mix(fillColor, base, 0.5f);

	// a pill-shaped groove with tick lines, as in the concept's nPlayer
	const float radius = floorf((orientation == B_HORIZONTAL
		? rect.Height() : rect.Width()) / 2);
	view->SetHighColor(fillColor);
	view->FillRoundRect(rect, radius, radius);

	view->SetHighColor(mix(fillColor, kBlack, 0.12f));
	if (orientation == B_HORIZONTAL) {
		for (float x = rect.left + radius + 2; x < rect.right - radius - 1;
				x += 6) {
			view->StrokeLine(BPoint(floorf(x), rect.top + 2),
				BPoint(floorf(x), rect.bottom - 2));
		}
	} else {
		for (float y = rect.top + radius + 2; y < rect.bottom - radius - 1;
				y += 6) {
			view->StrokeLine(BPoint(rect.left + 2, floorf(y)),
				BPoint(rect.right - 2, floorf(y)));
		}
	}

	view->SetHighColor(border_color(base, flags & ~B_ACTIVATED));
	view->StrokeRoundRect(rect, radius, radius);

	view->PopState();
}


void
GonxControlLook::DrawSliderThumb(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	orientation orientation)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	view->PushState();

	BRect knob(rect);
	knob.right -= 1;
	knob.bottom -= 1;
	const float radius = floorf(std::min(knob.Width(), knob.Height()) / 2);

	const bool disabled = (flags & B_DISABLED) != 0;
	if (!disabled) {
		view->SetDrawingMode(B_OP_ALPHA);
		view->SetHighColor((rgb_color){ 0, 0, 0, 50 });
		view->FillRoundRect(knob.OffsetByCopy(1, 1), radius, radius);
		view->SetDrawingMode(B_OP_COPY);
	}

	BGradientLinear gradient;
	gradient.AddColor(kWhite, 0);
	gradient.AddColor(mix(base, kWhite, disabled ? 0.6f : 0.3f), 255);
	gradient.SetStart(knob.LeftTop());
	gradient.SetEnd(knob.LeftBottom());
	view->FillRoundRect(knob, radius, radius, gradient);

	rgb_color border = border_color(base, flags & ~B_ACTIVATED);
	if (!disabled && (flags & B_FOCUSED) != 0)
		border = ui_color(B_KEYBOARD_NAVIGATION_COLOR);
	view->SetHighColor(border);
	view->StrokeRoundRect(knob, radius, radius);

	view->PopState();

	rect = knob.InsetByCopy(2, 2);
}


void
GonxControlLook::DrawSliderTriangle(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	orientation orientation)
{
	DrawSliderTriangle(view, rect, updateRect, base, kMarker, flags,
		orientation);
}


void
GonxControlLook::DrawSliderTriangle(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, const rgb_color& fill,
	uint32 flags, orientation orientation)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	// the yellow position marker from the concept's player
	rgb_color fillColor = fill;
	rgb_color outline = kMarkerOutline;
	if ((flags & B_DISABLED) != 0) {
		fillColor = mix(fill, base, 0.6f);
		outline = mix(outline, base, 0.6f);
	}

	view->PushState();
	const uint32 viewFlags = view->Flags();
	view->SetFlags(viewFlags | B_SUBPIXEL_PRECISE);

	BPoint a, b, c;
	if (orientation == B_HORIZONTAL) {
		a.Set((rect.left + rect.right) / 2 + 0.5f, rect.top + 0.5f);
		b.Set(rect.left + 0.5f, rect.bottom - 0.5f);
		c.Set(rect.right - 0.5f, rect.bottom - 0.5f);
	} else {
		a.Set(rect.left + 0.5f, (rect.top + rect.bottom) / 2 + 0.5f);
		b.Set(rect.right - 0.5f, rect.top + 0.5f);
		c.Set(rect.right - 0.5f, rect.bottom - 0.5f);
	}
	view->SetHighColor(fillColor);
	view->FillTriangle(a, b, c);
	view->SetHighColor(outline);
	view->StrokeTriangle(a, b, c);

	view->SetFlags(viewFlags);
	view->PopState();
}


//	#pragma mark - tabs


void
GonxControlLook::DrawTabFrame(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders, border_style borderStyle, uint32 side)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	// the strip behind the tabs: plain, with one line along the content
	view->SetHighColor(base);
	view->FillRect(rect);

	if (borderStyle == B_NO_BORDER)
		return;

	view->SetHighColor(mix(kBorder, base, 0.25f));
	switch (side) {
		default:
		case BTabView::kTopSide:
			view->StrokeLine(rect.LeftBottom(), rect.RightBottom());
			break;
		case BTabView::kBottomSide:
			view->StrokeLine(rect.LeftTop(), rect.RightTop());
			break;
		case BTabView::kLeftSide:
			view->StrokeLine(rect.RightTop(), rect.RightBottom());
			break;
		case BTabView::kRightSide:
			view->StrokeLine(rect.LeftTop(), rect.LeftBottom());
			break;
	}
}


void
GonxControlLook::DrawActiveTab(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders, uint32 side, int32, int32, int32, int32)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;
	_DrawTab(view, rect, base, flags, borders, side, true);
}


void
GonxControlLook::DrawInactiveTab(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders, uint32 side, int32, int32, int32, int32)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;
	_DrawTab(view, rect, base, flags, borders, side, false);
}


//	#pragma mark - private


/*!	Dano's button: a near-black outline with cut corners and a grey drop
	shadow at the bottom right, in two pixels (the top and left one shows the
	background). A default button takes five: a rounded blue frame around
	the outline.
*/
void
GonxControlLook::_DrawGonxButtonFrame(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base,
	const rgb_color& background, uint32 flags, uint32 borders,
	bool fitToConcept, float radius)
{
	if (!rect.IsValid())
		return;

	if (is_flat(flags)) {
		_DrawFrame(view, rect, background, background, background,
			background, borders);
		_DrawFrame(view, rect, background, background, background,
			background, borders);
		return;
	}

	const bool blend = (flags & B_BLEND_FRAME) != 0;
	const bool disabled = (flags & B_DISABLED) != 0;
	const bool isDefault = (flags & B_DEFAULT_BUTTON) != 0;

	view->PushState();
	view->ClipToRect(rect);
	if (blend)
		view->SetDrawingMode(B_OP_ALPHA);
	else {
		view->SetHighColor(background);
		view->FillRect(rect);
	}

	if (fitToConcept)
		_FitButtonHeight(view, rect, flags, borders);

	// The outline is two pixels thick all round. On a normal button the
	// frame has room for only one, so the face's first row carries the
	// second (see _DrawFace()); a default button's frame holds both, with
	// face colour inside them.
	const rgb_color outline = button_outline(background, flags);
	rgb_color shadow = isDefault ? kDefaultGlow : shadow_color(background);
	if (blend)
		shadow = (rgb_color){ 0, 0, 0, 50 };

	// the shape sits one pixel in from the top left; the shadow is the same
	// shape one pixel down and right
	BRect shape(rect.left + 1, rect.top + 1, rect.right - 1, rect.bottom - 1);
	shape = extend_past_open_sides(shape, borders, kButtonRadius + 4);
	if (!disabled)
		fill_rounded(view, shape.OffsetByCopy(1, 1), radius, shadow);
	fill_rounded(view, shape, radius, outline);

	if (isDefault) {
		const rgb_color face = face_color(base, flags, 0.65f);
		fill_rounded(view, shape.InsetByCopy(2, 2),
			std::max(0.0f, radius - 2), face);

		// it shades to light blue along the inside of its bottom edge, as
		// the concept's "Search" button does
		if (!disabled && (flags & B_ACTIVATED) == 0) {
			view->SetHighColor(mix(face, kDefaultLight, 0.75f));
			view->FillRect(BRect(shape.left + 3, shape.bottom - 2,
				shape.right - 3, shape.bottom - 2));
		}
		inset_by_borders(rect, 5, borders);
	} else
		inset_by_borders(rect, 2, borders);

	view->PopState();
}


/*!	The concept's buttons are much lower than the ones BButton sizes for
	itself: about the text's height plus three pixels, outline included (20
	pixels at 12 pt), two more for a default button. When \a rect is the
	height BButton would choose on its own, the button is drawn at the
	concept's height, centred; BButton centres its label in whatever rect it
	gets back, so the label follows. Taller buttons (icons, explicit sizes)
	are left alone.
*/
void
GonxControlLook::_FitButtonHeight(BView* view, BRect& rect, uint32 flags,
	uint32 borders)
{
	if ((borders & (B_TOP_BORDER | B_BOTTOM_BORDER))
			!= (B_TOP_BORDER | B_BOTTOM_BORDER))
		return;

	font_height fontHeight;
	view->GetFontHeight(&fontHeight);
	const float textHeight = ceilf(fontHeight.ascent + fontHeight.descent);
	const bool isDefault = (flags & B_DEFAULT_BUTTON) != 0;

	// what BButton::_ValidatePreferredSize() would pick, give or take
	const float automatic = std::max(ceilf(textHeight * 1.8f),
		textHeight + (isDefault ? 18 : 12));
	// outline box, plus the background row above and the shadow row below
	// at least the concept's 20 pixels, outline included
	const float wanted = std::max(textHeight + 3, 20.0f)
		+ (isDefault ? 2 : 0) + 2;

	const float height = rect.Height() + 1;
	if (height <= wanted || height > automatic + 1)
		return;

	const float extra = height - wanted;
	rect.top += floorf(extra / 2);
	rect.bottom -= ceilf(extra / 2);
}


/*!	A flat face inside the outline. Its corner pixels are left alone: they
	belong to the outline's cut corners. Pressed faces darken; hovering tints
	them towards the Gonx blue.
*/
void
GonxControlLook::_DrawFace(BView* view, BRect& rect, const rgb_color& base,
	uint32 flags, uint32 borders, orientation orientation, float lightness,
	bool rounded)
{
	if (!rect.IsValid())
		return;

	if (is_flat(flags)) {
		_DrawFrame(view, rect, base, base, base, base, borders);
		view->SetHighColor(base);
		view->FillRect(rect);
		return;
	}

	const rgb_color face = face_color(base, flags, lightness);

	// On a normal button or field the face's first row is the outline's
	// second pixel; a default button's frame already holds both.
	const bool isDefault = (flags & B_DEFAULT_BUTTON) != 0;
	const rgb_color ring = isDefault ? face : button_outline(base, flags);

	view->SetHighColor(ring);
	view->FillRect(rect);

	BRect inner(rect);
	inset_by_borders(inner, 1, borders);
	if (!inner.IsValid()) {
		rect = inner;
		return;
	}
	view->SetHighColor(face);
	view->FillRect(inner);

	// the two-pixel outline's inside corners are rounded too: give the
	// corner pixel back to the outline where the corner really is rounded
	if (rounded && !isDefault && inner.Height() >= 2 && inner.Width() >= 2) {
		const bool left = (borders & B_LEFT_BORDER) != 0;
		const bool top = (borders & B_TOP_BORDER) != 0;
		const bool right = (borders & B_RIGHT_BORDER) != 0;
		const bool bottom = (borders & B_BOTTOM_BORDER) != 0;
		view->SetHighColor(ring);
		if (left && top)
			view->FillRect(BRect(inner.left, inner.top, inner.left, inner.top));
		if (right && top) {
			view->FillRect(BRect(inner.right, inner.top, inner.right,
				inner.top));
		}
		if (left && bottom) {
			view->FillRect(BRect(inner.left, inner.bottom, inner.left,
				inner.bottom));
		}
		if (right && bottom) {
			view->FillRect(BRect(inner.right, inner.bottom, inner.right,
				inner.bottom));
		}
	}

	rect = inner;
}


/*!	The arrow's segment, as in the concept: a light grey strip inside the
	outline holding an outlined white triangle, with a grey divider on the
	side facing the label. \a box spans the face's full height; \a face is
	the face's rect, whose first row and corner pixels belong to the
	outline.
*/
void
GonxControlLook::_DrawIndicator(BView* view, BRect box, const BRect& face,
	const rgb_color& base, uint32 flags, uint32 direction, bool rounded)
{
	static const rgb_color kSegment = { 213, 213, 203, 255 };
	static const rgb_color kDivider = { 155, 155, 143, 255 };
	static const rgb_color kArrowOutline = { 149, 149, 139, 255 };
	static const rgb_color kArrowFill = { 240, 240, 230, 255 };

	const bool disabled = (flags & B_DISABLED) != 0;
	const rgb_color segment = disabled ? mix(kSegment, base, 0.5f) : kSegment;
	const rgb_color divider = disabled ? mix(kDivider, base, 0.5f) : kDivider;
	const bool onLeft = box.left <= face.left;

	// stay inside the outline's second pixel, which runs round the face
	const BRect inner = face.InsetByCopy(1, 1);
	box = box & inner;
	if (!box.IsValid())
		return;

	view->PushState();

	view->SetHighColor(segment);
	view->FillRect(box);

	// the inner corners next to the outline stay the outline's
	view->SetHighColor(button_outline(base, flags));
	const float corner = onLeft ? inner.left : inner.right;
	if (rounded && box.left <= corner && corner <= box.right) {
		view->FillRect(BRect(corner, inner.top, corner, inner.top));
		view->FillRect(BRect(corner, inner.bottom, corner, inner.bottom));
	}

	view->SetHighColor(divider);
	const float x = onLeft ? box.right : box.left;
	view->FillRect(BRect(x, box.top, x, box.bottom));

	BRect arrow(box);
	if (onLeft)
		arrow.right--;
	else
		arrow.left++;
	_DrawTriangle(view, arrow, direction,
		disabled ? mix(kArrowOutline, base, 0.5f) : kArrowOutline,
		disabled ? mix(kArrowFill, base, 0.5f) : kArrowFill);

	view->PopState();
}


/*!	An outlined triangle, white inside, centred in \a rect: the concept uses
	these for every arrow, from scroll bars to menu fields. It is set pixel
	by pixel, so it stays crisp and free of the colour fringes subpixel
	anti-aliasing would give it.
*/
void
GonxControlLook::_DrawTriangle(BView* view, BRect rect, uint32 direction,
	const rgb_color& outline, const rgb_color& fill)
{
	// The base runs across the arrow's direction and follows that extent;
	// the point gets about 0.65 of the base's length (the concept's
	// triangles are tall and narrow), limited by the other extent.
	const bool horizontal = direction == B_LEFT_ARROW
		|| direction == B_RIGHT_ARROW;
	const float across = (horizontal ? rect.Height() : rect.Width()) + 1;
	const float along = (horizontal ? rect.Width() : rect.Height()) + 1;

	int32 length = (int32)std::min(across * 0.6f, along * 1.6f);
	if (length % 2 == 0)
		length--;
	length = std::max(length, (int32)5);
	const int32 depth = (int32)std::max(3.0f,
		std::min(roundf(length * 0.65f), along - 2));

	const int32 originAcross = (int32)floorf((horizontal
		? (rect.top + rect.bottom) : (rect.left + rect.right)) / 2)
		- length / 2;
	const int32 originAlong = (int32)floorf((horizontal
		? (rect.left + rect.right) : (rect.top + rect.bottom)) / 2)
		- depth / 2;

	// extent of each column across the base, from base to tip
	int32 extent[length];
	const float middle = (length - 1) / 2.0f;
	for (int32 k = 0; k < length; k++) {
		extent[k] = std::max((int32)1, (int32)roundf(depth
			* (1 - fabsf(k - middle) / (middle + 0.5f))));
	}

	view->PushState();
	view->SetDrawingMode(B_OP_COPY);

	for (int32 k = 0; k < length; k++) {
		for (int32 j = 0; j < extent[k]; j++) {
			const bool edge = j == 0 || j == extent[k] - 1 || k == 0
				|| k == length - 1 || extent[k - 1] <= j
				|| extent[k + 1] <= j;

			// j runs from the base towards the point
			int32 x, y;
			const int32 a = originAcross + k;
			switch (direction) {
				case B_RIGHT_ARROW:
					x = originAlong + j;
					y = a;
					break;
				case B_LEFT_ARROW:
					x = originAlong + depth - 1 - j;
					y = a;
					break;
				case B_UP_ARROW:
					x = a;
					y = originAlong + depth - 1 - j;
					break;
				case B_DOWN_ARROW:
				default:
					x = a;
					y = originAlong + j;
					break;
			}
			view->SetHighColor(edge ? outline : fill);
			view->FillRect(BRect(x, y, x, y));
		}
	}

	view->PopState();
}


/*!	Rounded on the side facing away from the content. Inactive tabs are
	shorter on that side, as in Haiku's look; the rect left for the label is
	inset the way Haiku's is, so labels don't move.
*/
void
GonxControlLook::_DrawTab(BView* view, BRect& rect, const rgb_color& base,
	uint32 flags, uint32 borders, uint32 side, bool active)
{
	rect.left = floorf(rect.left);
	rect.right = floorf(rect.right);
	rect.top = floorf(rect.top);
	rect.bottom = floorf(rect.bottom);

	view->PushState();
	view->ClipToRect(rect);

	BRect shape(rect);
	if (!active) {
		BRect strip(rect);
		switch (side) {
			default:
			case BTabView::kTopSide:
				shape.top += 4;
				strip.bottom = shape.top - 1;
				break;
			case BTabView::kBottomSide:
				shape.bottom -= 4;
				strip.top = shape.bottom + 1;
				break;
			case BTabView::kLeftSide:
				shape.left += 4;
				strip.right = shape.left - 1;
				break;
			case BTabView::kRightSide:
				shape.right -= 4;
				strip.left = shape.right + 1;
				break;
		}
		view->SetHighColor(base);
		view->FillRect(strip);
	}

	// Extend the shape past the content side, so only the outer corners
	// come out rounded and the active tab runs into the content unbroken.
	BRect outline(shape);
	const float reach = kTabRadius + 2;
	switch (side) {
		default:
		case BTabView::kTopSide:
			outline.bottom += reach;
			break;
		case BTabView::kBottomSide:
			outline.top -= reach;
			break;
		case BTabView::kLeftSide:
			outline.right += reach;
			break;
		case BTabView::kRightSide:
			outline.left -= reach;
			break;
	}

	const bool disabled = (flags & B_DISABLED) != 0;
	rgb_color border = active ? kBorder : mix(kBorder, base, 0.25f);
	rgb_color top = active ? mix(base, kWhite, 0.8f) : mix(base, kBlack, 0.02f);
	rgb_color bottom = active ? mix(base, kWhite, 0.45f)
		: mix(base, kBlack, 0.07f);
	if (disabled) {
		border = mix(border, base, 0.5f);
		top = mix(top, base, 0.5f);
		bottom = mix(bottom, base, 0.5f);
	}

	view->ClipToRect(shape);
	view->SetHighColor(border);
	view->FillRoundRect(outline, kTabRadius, kTabRadius);

	BRect inner = outline.InsetByCopy(1, 1);
	BGradientLinear gradient;
	gradient.AddColor(top, 0);
	gradient.AddColor(bottom, 255);
	const bool vertical = side == BTabView::kLeftSide
		|| side == BTabView::kRightSide;
	gradient.SetStart(shape.LeftTop());
	gradient.SetEnd(vertical ? shape.RightTop() : shape.LeftBottom());
	view->FillRoundRect(inner, kTabRadius - 1, kTabRadius - 1, gradient);

	if (!active) {
		// inactive tabs sit behind the content's edge line
		view->SetHighColor(mix(kBorder, base, 0.25f));
		switch (side) {
			default:
			case BTabView::kTopSide:
				view->StrokeLine(shape.LeftBottom(), shape.RightBottom());
				break;
			case BTabView::kBottomSide:
				view->StrokeLine(shape.LeftTop(), shape.RightTop());
				break;
			case BTabView::kLeftSide:
				view->StrokeLine(shape.RightTop(), shape.RightBottom());
				break;
			case BTabView::kRightSide:
				view->StrokeLine(shape.LeftTop(), shape.LeftBottom());
				break;
		}
	}

	view->PopState();

	// leave the label the same room Haiku's look does
	rect = shape;
	const float outer = active ? 3 : 2;
	const float content = active ? 1 : 2;
	switch (side) {
		default:
		case BTabView::kTopSide:
			rect.left += outer; rect.right -= outer;
			rect.top += outer; rect.bottom -= content;
			break;
		case BTabView::kBottomSide:
			rect.left += outer; rect.right -= outer;
			rect.bottom -= outer; rect.top += content;
			break;
		case BTabView::kLeftSide:
			rect.top += outer; rect.bottom -= outer;
			rect.left += outer; rect.right -= content;
			break;
		case BTabView::kRightSide:
			rect.top += outer; rect.bottom -= outer;
			rect.right -= outer; rect.left += content;
			break;
	}
}


}	// namespace BPrivate


extern "C" BControlLook*
instantiate_control_look(image_id id)
{
	return new (std::nothrow) BPrivate::GonxControlLook();
}
