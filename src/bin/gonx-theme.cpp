/*
 * Copyright 2026, Gonx contributors.
 * Distributed under the terms of the MIT License.
 *
 * gonx-theme: switch the desktop to the Gonx look and back.
 *
 *   gonx-theme apply    Gonx decorator, control look, desktop colour,
 *                       system colours and fonts
 *   gonx-theme revert   restore what was there before the first apply
 *   gonx-theme status
 *
 * The state before the first "apply" is kept in Gonx/previous in the user
 * settings folder (~/.config/cosmoe) so "revert" can always get back to it,
 * however many times "apply" runs in between.
 *
 * The same tool as in github.com/idumych/gonx, for Hokku: the decorator is
 * Hokku's built-in Gonx frame (Wayland) and the control look is the
 * GonxControlLook add-on built with Hokku.
 */


#include <stdio.h>
#include <string.h>

#include <Application.h>
#include <Font.h>
#include <Menu.h>
#include <File.h>
#include <FindDirectory.h>
#include <InterfaceDefs.h>
#include <Message.h>
#include <Path.h>
#include <Screen.h>
#include <String.h>

#include <DecorInfo.h>
#include <DecoratorPrivate.h>
#include <InterfacePrivate.h>


static const char* kSignature = "application/x-vnd.Gonx-Theme";
static const char* kDecoratorName = "Gonx";
static const char* kControlLookName = "GonxControlLook.so";

// Desktop and system colours sampled from the concept screenshots.
static const rgb_color kDesktopColor = { 128, 157, 189, 255 };

static const struct {
	color_which	which;
	rgb_color	color;
} kColors[] = {
	{ B_PANEL_BACKGROUND_COLOR,			{ 236, 235, 230, 255 } },
	{ B_MENU_BACKGROUND_COLOR,			{ 247, 247, 245, 255 } },
	{ B_MENU_SELECTED_BACKGROUND_COLOR,	{ 158, 178, 202, 255 } },
	{ B_MENU_SELECTED_BORDER_COLOR,		{ 79, 91, 107, 255 } },
	{ B_LIST_SELECTED_BACKGROUND_COLOR,	{ 170, 187, 208, 255 } },
	{ B_NAVIGATION_BASE_COLOR,			{ 79, 108, 151, 255 } },
	{ B_WINDOW_TAB_COLOR,				{ 251, 204, 0, 255 } },
	{ B_WINDOW_TEXT_COLOR,				{ 0, 0, 0, 255 } },
	{ B_WINDOW_INACTIVE_TAB_COLOR,		{ 235, 235, 235, 255 } },
	{ B_WINDOW_INACTIVE_TEXT_COLOR,		{ 76, 76, 76, 255 } },
	{ B_WINDOW_BORDER_COLOR,			{ 158, 178, 202, 255 } },
	{ B_WINDOW_INACTIVE_BORDER_COLOR,	{ 166, 183, 204, 255 } },
};
static const int32 kColorCount = sizeof(kColors) / sizeof(kColors[0]);

// System fonts sized to the concept's text: its plain text has a 6 to 7
// pixel cap height and its bold window titles 8 to 9, which Noto Sans
// reaches at 9 and 11 points.
static const struct {
	const char*	which;
	const char*	family;
	const char*	style;
	float		size;
} kFonts[] = {
	{ "plain", "Noto Sans", "Regular", 9 },
	{ "bold", "Noto Sans", "Bold", 11 },
	{ "menu", "Noto Sans", "Regular", 9 },
};
static const int32 kFontCount = sizeof(kFonts) / sizeof(kFonts[0]);


// private font API, as used by Appearance
extern void _set_system_font_(const char* which, font_family family,
	font_style style, float size);


/*!	Sets one of the system fonts. The menu font isn't a system font in
	app_server: it is part of the menu settings, as in Appearance.
*/
static void
set_font(const char* which, const char* familyName, const char* styleName,
	float size)
{
	font_family family;
	font_style style;
	strlcpy(family, familyName, sizeof(family));
	strlcpy(style, styleName, sizeof(style));

	if (strcmp(which, "menu") == 0) {
		menu_info info;
		if (get_menu_info(&info) != B_OK)
			return;
		strlcpy(info.f_family, family, sizeof(info.f_family));
		strlcpy(info.f_style, style, sizeof(info.f_style));
		info.font_size = size;
		set_menu_info(&info);
	} else
		_set_system_font_(which, family, style, size);
}


static void
save_font(BMessage& message, const char* which, const BFont* font)
{
	font_family family;
	font_style style;
	font->GetFamilyAndStyle(&family, &style);

	BMessage saved;
	saved.AddString("which", which);
	saved.AddString("family", family);
	saved.AddString("style", style);
	saved.AddFloat("size", font->Size());
	message.AddMessage("font", &saved);
}


/*!	Finds the Gonx control look in the add-on folders, user ones first. */
static bool
find_control_look(BString& path)
{
	static const directory_which kDirectories[] = {
		B_USER_NONPACKAGED_ADDONS_DIRECTORY,
		B_USER_ADDONS_DIRECTORY,
		B_SYSTEM_NONPACKAGED_ADDONS_DIRECTORY,
		B_SYSTEM_ADDONS_DIRECTORY
	};

	for (size_t i = 0; i < sizeof(kDirectories) / sizeof(kDirectories[0]);
			i++) {
		BPath candidate;
		if (find_directory(kDirectories[i], &candidate) != B_OK)
			continue;
		candidate.Append("control_look");
		candidate.Append(kControlLookName);
		if (BEntry(candidate.Path()).Exists()) {
			path = candidate.Path();
			return true;
		}
	}
	return false;
}


static status_t
previous_path(BPath& path, bool create)
{
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path, create);
	if (status != B_OK)
		return status;
	path.Append("Gonx");
	if (create)
		create_directory(path.Path(), 0755);
	return path.Append("previous");
}


static bool
load_previous(BMessage& previous)
{
	BPath path;
	if (previous_path(path, false) != B_OK)
		return false;

	BFile file(path.Path(), B_READ_ONLY);
	return file.InitCheck() == B_OK && previous.Unflatten(&file) == B_OK;
}


static void
save_fonts(BMessage& message)
{
	save_font(message, "plain", be_plain_font);
	save_font(message, "bold", be_bold_font);
	menu_info menu;
	if (get_menu_info(&menu) == B_OK) {
		BFont menuFont;
		menuFont.SetFamilyAndStyle(menu.f_family, menu.f_style);
		menuFont.SetSize(menu.font_size);
		save_font(message, "menu", &menuFont);
	}
}


static status_t
write_previous(const BMessage& previous)
{
	BPath path;
	status_t status = previous_path(path, true);
	if (status != B_OK)
		return status;

	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status = file.InitCheck();
	if (status != B_OK)
		return status;
	return previous.Flatten(&file);
}


static status_t
save_current()
{
	BMessage current;

	BString decorator;
	if (BPrivate::get_decorator(decorator))
		current.AddString("decorator", decorator);

	// an empty path stands for Haiku's built-in look
	BString controlLook;
	BPrivate::get_control_look(controlLook);
	current.AddString("control look", controlLook);

	current.AddColor("desktop", BScreen().DesktopColor());

	BMessage colors;
	for (int32 i = 0; i < kColorCount; i++)
		colors.AddColor(ui_color_name(kColors[i].which),
			ui_color(kColors[i].which));
	current.AddMessage("colors", &colors);

	save_fonts(current);

	BPath path;
	status_t status = previous_path(path, true);
	if (status != B_OK)
		return status;

	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status = file.InitCheck();
	if (status != B_OK)
		return status;
	return current.Flatten(&file);
}


static int
apply()
{
	BMessage previous;
	if (load_previous(previous) && !previous.HasMessage("font")) {
		// saved by an older gonx-theme, before it set fonts: keep the fonts
		// in use now, so "revert" can put them back
		save_fonts(previous);
		write_previous(previous);
	} else if (!load_previous(previous)) {
		status_t status = save_current();
		if (status != B_OK) {
			fprintf(stderr, "gonx-theme: can't save the current theme: %s\n",
				strerror(status));
			return 1;
		}
	}

	DecorInfoUtility decorators;
	DecorInfo* decor = decorators.FindDecorator(kDecoratorName);
	if (decor == NULL) {
		fprintf(stderr, "gonx-theme: the %s decorator isn't installed\n",
			kDecoratorName);
		return 1;
	}
	status_t status = decorators.SetDecorator(decor);
	if (status != B_OK) {
		fprintf(stderr, "gonx-theme: can't switch the decorator: %s\n",
			strerror(status));
		return 1;
	}

	BMessage colors;
	for (int32 i = 0; i < kColorCount; i++)
		colors.AddColor(ui_color_name(kColors[i].which), kColors[i].color);
	set_ui_colors(&colors);

	BScreen().SetDesktopColor(kDesktopColor, true);

	for (int32 i = 0; i < kFontCount; i++) {
		set_font(kFonts[i].which, kFonts[i].family, kFonts[i].style,
			kFonts[i].size);
	}

	// Applications load the control look when they start, so running ones
	// keep the old one until they are restarted.
	BString controlLook;
	if (find_control_look(controlLook))
		BPrivate::set_control_look(controlLook);
	else
		fprintf(stderr, "gonx-theme: the control look isn't installed\n");

	printf("Gonx look applied. Applications that are already running pick "
		"up the new controls when restarted.\n"
		"\"gonx-theme revert\" undoes it.\n");
	return 0;
}


static int
revert()
{
	BMessage previous;
	if (!load_previous(previous)) {
		fprintf(stderr, "gonx-theme: nothing to revert to\n");
		return 1;
	}

	BString decorator;
	if (previous.FindString("decorator", &decorator) == B_OK)
		BPrivate::set_decorator(decorator);

	BMessage colors;
	if (previous.FindMessage("colors", &colors) == B_OK)
		set_ui_colors(&colors);

	BString controlLook;
	if (previous.FindString("control look", &controlLook) == B_OK)
		BPrivate::set_control_look(controlLook);

	BMessage font;
	for (int32 i = 0; previous.FindMessage("font", i, &font) == B_OK; i++) {
		BString which, familyName, styleName;
		float size;
		if (font.FindString("which", &which) != B_OK
			|| font.FindString("family", &familyName) != B_OK
			|| font.FindString("style", &styleName) != B_OK
			|| font.FindFloat("size", &size) != B_OK)
			continue;
		set_font(which.String(), familyName.String(), styleName.String(),
			size);
	}

	rgb_color desktop;
	if (previous.FindColor("desktop", &desktop) == B_OK)
		BScreen().SetDesktopColor(desktop, true);

	BPath path;
	if (previous_path(path, false) == B_OK)
		BEntry(path.Path()).Remove();

	printf("Previous look restored.\n");
	return 0;
}


static int
status()
{
	DecorInfoUtility decorators;
	DecorInfo* current = decorators.CurrentDecorator();
	printf("decorator: %s\n",
		current != NULL ? current->Name().String() : "?");

	BString controlLook;
	BPrivate::get_control_look(controlLook);
	printf("control look: %s\n",
		controlLook.IsEmpty() ? "(built-in)" : controlLook.String());

	BMessage previous;
	printf("saved previous look: %s\n",
		load_previous(previous) ? "yes" : "no");
	return 0;
}


int
main(int argc, char** argv)
{
	const char* command = argc > 1 ? argv[1] : "";

	BApplication app(kSignature);

	if (strcmp(command, "apply") == 0)
		return apply();
	if (strcmp(command, "revert") == 0)
		return revert();
	if (strcmp(command, "status") == 0)
		return status();

	fprintf(stderr, "usage: gonx-theme apply|revert|status\n");
	return 2;
}
