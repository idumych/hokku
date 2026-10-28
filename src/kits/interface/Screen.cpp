/*
 * Copyright 2003-2009, Haiku Inc.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stefano Ceccherini (burton666@libero.it)
 *		Axel Dörfler, axeld@pinc-software.de
 */


#include <Screen.h>

#include <new>

#include <Application.h>
#include <Bitmap.h>
#include <Window.h>

#include <DesktopSettings.h>

//#include <PrivateScreen.h>


using namespace BPrivate;


/*!	\brief Creates a BScreen object which represents the main display with the given
		screen_id
	\param id The screen_id of the screen to get.

	In the current simplified implementation, there is only one display (B_MAIN_SCREEN_ID).
*/
BScreen::BScreen(screen_id id)
{
}


/*!	\brief Creates a BScreen object which represents the display which contains
	the given BWindow.
	\param window A BWindow.
*/
BScreen::BScreen(BWindow* window)
{
}


/*!	\brief Releases the resources allocated by the constructor.
*/ 
BScreen::~BScreen()
{
}


/*! \brief Checks if the BScreen object represents a real screen connected to
		the computer.
	\return \c true if the BScreen object is valid, \c false if not.
*/
bool
BScreen::IsValid()
{
	return true;
}


/*!	\brief Returns the color space of the screen display.
	\return \c B_CMAP8, \c B_RGB15, or \c B_RGB32, or \c B_NO_COLOR_SPACE
		if the screen object is invalid.
*/
color_space
BScreen::ColorSpace()
{
	return B_RGB32;
}


/*!	\brief Returns the rectangle that locates the screen in the screen
		coordinate system.
	\return a BRect that locates the screen in the screen coordinate system.
*/
BRect
BScreen::Frame()
{
	struct rectangle allocation;
	cosmoe_display_get_screen_dimensions(be_app->Display(), &allocation);

	// Like Haiku: the last pixel, not one past it
	return BRect(allocation.x, allocation.y,
		allocation.x + allocation.width - 1,
		allocation.y + allocation.height - 1);
}


/*!	\brief Allocates a B_RGB32 bitmap and copies the screen (or \a frame
		of it) into it. The caller owns the bitmap.
	Hokku: X11 only; Wayland doesn't let a client read the screen and
	returns \c B_UNSUPPORTED. The cursor is never drawn.
*/
status_t
BScreen::GetBitmap(BBitmap** _bitmap, bool drawCursor, BRect* frame)
{
	if (_bitmap == NULL)
		return B_BAD_VALUE;

	BRect rect = frame != NULL ? *frame : Frame();
	BBitmap* bitmap = new(std::nothrow) BBitmap(rect, B_RGB32);
	if (bitmap == NULL)
		return B_NO_MEMORY;

	status_t status = bitmap->InitCheck();
	if (status == B_OK)
		status = ReadBitmap(bitmap, drawCursor, &rect);
	if (status != B_OK) {
		delete bitmap;
		return status;
	}

	*_bitmap = bitmap;
	return B_OK;
}


/*!	\brief Copies the screen (or \a frame of it) into a B_RGB32 \a bitmap
		of the same size.
*/
status_t
BScreen::ReadBitmap(BBitmap* bitmap, bool drawCursor, BRect* frame)
{
	(void)drawCursor;
	if (bitmap == NULL || be_app == NULL)
		return B_BAD_VALUE;
	if (bitmap->ColorSpace() != B_RGB32 && bitmap->ColorSpace() != B_RGBA32)
		return B_BAD_VALUE;

	BRect rect = frame != NULL ? *frame : Frame();
	rect = rect & Frame();
	if (!rect.IsValid())
		return B_BAD_VALUE;

	int32 width = rect.IntegerWidth() + 1;
	int32 height = rect.IntegerHeight() + 1;
	if (width > bitmap->Bounds().IntegerWidth() + 1
		|| height > bitmap->Bounds().IntegerHeight() + 1) {
		return B_BAD_VALUE;
	}

	return cosmoe_display_read_screen(be_app->Display(), (int32)rect.left,
		(int32)rect.top, width, height, bitmap->Bits(), bitmap->BytesPerRow());
}


/*!	\brief Returns the identifier for the screen.
	\return A screen_id struct that identifies the screen.

	In the current implementation, this function always returns
	\c B_MAIN_SCREEN_ID, even if the object is invalid.
*/
screen_id
BScreen::ID()
{
	return B_MAIN_SCREEN_ID;
}


uint8
BScreen::IndexForColor(uint8 red, uint8 green, uint8 blue, uint8 alpha)
{
	if (red == B_TRANSPARENT_32_BIT.red
		&& green == B_TRANSPARENT_32_BIT.green
		&& blue == B_TRANSPARENT_32_BIT.blue
		&& alpha == B_TRANSPARENT_32_BIT.alpha) {
		return B_TRANSPARENT_8_BIT;
	}

	const color_map* colorMap = ColorMap();
	uint16 index = ((red & 0xf8) << 7) | ((green & 0xf8) << 2) | (blue >> 3);
	return colorMap != NULL ? colorMap->index_map[index] : 0;
}


rgb_color
BScreen::ColorForIndex(const uint8 index)
{
	const color_map* colorMap = ColorMap();
	if (colorMap != NULL)
		return colorMap->color_list[index];

	return rgb_color();
}


uint8
BScreen::InvertIndex(uint8 index)
{
	const color_map* colorMap = ColorMap();
	if (colorMap != NULL)
		return colorMap->inversion_map[index];

	return 0;
}


const color_map*
BScreen::ColorMap()
{
	return system_colors();
}


rgb_color
BScreen::DesktopColor()
{
	// one colour for every workspace, kept in the appearance settings
	DesktopSettings settings;
	return settings.DesktopColor();
}


rgb_color
BScreen::DesktopColor(uint32 workspace)
{
	return DesktopColor();
}


void
BScreen::SetDesktopColor(rgb_color color, bool stick)
{
	SetDesktopColor(color, 0, stick);
}


void
BScreen::SetDesktopColor(rgb_color color, uint32 workspace, bool stick)
{
	// Hokku's desktop (Tracker) draws the colour itself and reads it when
	// it starts; without "stick" Haiku changes it until the next reboot,
	// which here is the same as not at all
	if (!stick)
		return;

	LockedDesktopSettings settings;
	settings.SetDesktopColor(color);
}
