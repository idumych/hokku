/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef BITMAP_MENU_ITEM_H
#define BITMAP_MENU_ITEM_H


#include <MenuItem.h>


class BBitmap;


/*!	A menu item, or a submenu's item, that draws an icon in front of its
	label: tray item menus' dbusmenu entries and the Linux applications. The
	item owns the icon, which may be NULL; a B_BITMAP_HIDPI icon is drawn at
	its logical size.
*/
class TBitmapMenuItem : public BMenuItem {
public:
								TBitmapMenuItem(const char* label,
									BMessage* message, BBitmap* icon);
								TBitmapMenuItem(BMenu* submenu,
									BBitmap* icon);
	virtual						~TBitmapMenuItem();

	virtual	void				GetContentSize(float* width, float* height);
	virtual	void				DrawContent();

private:
			float				_Spacing() const;

			BBitmap*			fIcon;
};


#endif	// BITMAP_MENU_ITEM_H
