/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 */


#include "BitmapMenuItem.h"

#include <math.h>

#include <algorithm>

#include <Bitmap.h>
#include <Menu.h>


TBitmapMenuItem::TBitmapMenuItem(const char* label, BMessage* message,
	BBitmap* icon)
	:
	BMenuItem(label, message),
	fIcon(icon)
{
}


TBitmapMenuItem::TBitmapMenuItem(BMenu* submenu, BBitmap* icon)
	:
	BMenuItem(submenu),
	fIcon(icon)
{
}


TBitmapMenuItem::~TBitmapMenuItem()
{
	delete fIcon;
}


void
TBitmapMenuItem::GetContentSize(float* width, float* height)
{
	BMenuItem::GetContentSize(width, height);
	if (fIcon != NULL) {
		*width += bitmap_logical_width(fIcon) + _Spacing();
		*height = std::max(*height, bitmap_logical_height(fIcon));
	}
}


void
TBitmapMenuItem::DrawContent()
{
	if (fIcon != NULL) {
		BMenu* menu = Menu();
		BPoint location = ContentLocation();
		BRect frame = Frame();
		float iconWidth = bitmap_logical_width(fIcon);
		float iconHeight = bitmap_logical_height(fIcon);
		BRect iconFrame(0, 0, iconWidth - 1, iconHeight - 1);
		iconFrame.OffsetTo(location.x,
			frame.top + floorf((frame.Height() + 1 - iconHeight) / 2));

		menu->PushState();
		menu->SetDrawingMode(B_OP_ALPHA);
		menu->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		if (!IsEnabled())
			menu->SetHighColor(0, 0, 0, 96);
		menu->DrawBitmap(fIcon, fIcon->Bounds(), iconFrame,
			B_FILTER_BITMAP_BILINEAR);
		menu->PopState();

		menu->MovePenTo(location.x + iconWidth + _Spacing(),
			menu->PenLocation().y);
	}
	BMenuItem::DrawContent();
}


float
TBitmapMenuItem::_Spacing() const
{
	return 6;
}
