/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef THEME_H
#define THEME_H


#include <GraphicsDefs.h>
#include <Message.h>
#include <ObjectList.h>
#include <Path.h>
#include <String.h>


/*!	A theme: a folder holding theme.ini, and the files it names (a labwc
	themerc, a wallpaper). Themes live in the system data folder
	(/usr/share/cosmoe/themes) and the user's settings folder
	(~/.config/cosmoe/themes); a user theme hides a system one of the same
	name.

	theme.ini sets everything a look is made of:

		[Theme]		Name, Description
		[Window]	Decorator (Default, Haiku, Gonx), ControlLook (an add-on's
					file name, empty for the built-in look)
		[Colors]	B_..._COLOR=#rrggbb; colours not listed are Haiku's
		[Fonts]		Plain, Bold, Fixed, Menu = family, style, size; fonts not
					listed are the defaults
		[Desktop]	Color=#rrggbb, Wallpaper (a file in the theme's folder or
					an absolute path; empty for none), WallpaperMode
					(scaled, centered, tiled)
		[Linux]		Labwc (a themerc in the theme's folder for labwc's frames
					round other apps), GtkTheme, IconTheme, CursorTheme,
					QtStyle, KvantumTheme
*/
class Theme {
public:
	enum {
		kPlainFont = 0,
		kBoldFont,
		kFixedFont,
		kMenuFont,
		kFontCount
	};

	struct Font {
		bool		set;
		BString		family;
		BString		style;
		float		size;
	};

								Theme();

			status_t			Load(const BPath& folder);
			status_t			Save(const BPath& folder) const;

	// What is in use now, as a theme called name.
			status_t			Capture(const char* name);

	// Applies everything the theme sets. Running apps pick up the colours,
	// fonts and frames of new windows; a new control look needs a restart.
			status_t			Apply() const;

			bool				IsUserTheme() const { return fUserTheme; }
			const BPath&		Folder() const { return fFolder; }

			BString				name;
			BString				description;
			BString				decorator;
			BString				controlLook;
			BMessage			colors;
			Font				fonts[kFontCount];
			bool				hasDesktopColor;
			rgb_color			desktopColor;
			BString				wallpaper;
			BString				wallpaperMode;
			BString				labwc;
			BString				gtkTheme;
			BString				iconTheme;
			BString				cursorTheme;
			BString				qtStyle;
			BString				kvantumTheme;

private:
			BPath				_Resolve(const BString& file) const;

			BPath				fFolder;
			bool				fUserTheme;
};


typedef BObjectList<Theme, true> ThemeList;


// The installed themes, sorted by name, user themes over system ones.
status_t	find_themes(ThemeList& themes);
status_t	user_themes_folder(BPath& path, bool create = false);

// The name of the theme last applied, or empty.
BString		current_theme_name();
status_t	set_current_theme_name(const char* name);

// Turns window and pointer shadows on or off: Hokku's frames (for apps
// started afterwards), labwc-hokku's round other apps and the cursor theme's
// variant, at once.
status_t	apply_window_shadows(bool shadows);


#endif	// THEME_H
