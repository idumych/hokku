/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Themes: reading, writing, capturing and applying them. Shared by
 * Appearance's Themes tab and hokku-theme.
 */


#include "Theme.h"

#include <errno.h>
#include <fs_attr.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include <Background.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Font.h>
#include <Menu.h>
#include <Messenger.h>
#include <Node.h>
#include <Screen.h>
#include <StringList.h>
#include <Window.h>

#include <DecoratorPrivate.h>
#include <DefaultColors.h>
#include <InterfacePrivate.h>
#include <ServerReadOnlyMemory.h>
#include <tracker_private.h>


extern char** environ;

// private font API, as used by Appearance
extern void _set_system_font_(const char* which, font_family family,
	font_style style, float size);


static const char* kThemeFile = "theme.ini";
static const char* kThemesFolder = "themes";
static const char* kCurrentThemeFile = "Appearance theme";

static const char* kFontKeys[Theme::kFontCount] = {
	"Plain", "Bold", "Fixed", "Menu"
};
static const char* kFontNames[Theme::kFontCount] = {
	"plain", "bold", "fixed", "menu"
};

// labwc reads this after the theme's themerc, whatever the theme is called
static const char* kLabwcOverride = "labwc/themerc-override";
static const char* kLabwcHeader = "# Written by Hokku's themes";

// Tracker's background settings (see Backgrounds)
static const char* kBackgroundInfo = "be:bgndimginfo";
enum { kWallpaperCentered = 1, kWallpaperScaled = 2, kWallpaperTiled = 3 };


//	#pragma mark - files


static status_t
read_file(const char* path, BString& text)
{
	BFile file(path, B_READ_ONLY);
	status_t status = file.InitCheck();
	if (status != B_OK)
		return status;

	off_t size;
	status = file.GetSize(&size);
	if (status != B_OK)
		return status;
	char* buffer = text.LockBuffer(size + 1);
	ssize_t bytes = file.Read(buffer, size);
	text.UnlockBuffer(bytes > 0 ? bytes : 0);
	return bytes < 0 ? (status_t)bytes : B_OK;
}


static status_t
write_file(const char* path, const BString& text)
{
	BFile file(path, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status_t status = file.InitCheck();
	if (status != B_OK)
		return status;
	return file.Write(text.String(), text.Length()) == text.Length()
		? B_OK : B_IO_ERROR;
}


//	#pragma mark - ini files


/*!	A small INI file: [sections] of key=value lines. Comments and the order
	of lines survive a SetValue(), so other programs' settings files can be
	edited in place.
*/
class IniFile {
public:
	status_t Load(const char* path)
	{
		fLines.MakeEmpty();
		BString text;
		status_t status = read_file(path, text);
		if (status != B_OK)
			return status;
		text.Split("\n", false, fLines);
		// Split() leaves an empty last line after the final newline
		if (fLines.CountStrings() > 0
			&& fLines.StringAt(fLines.CountStrings() - 1).IsEmpty())
			fLines.Remove(fLines.CountStrings() - 1);
		return B_OK;
	}

	status_t Save(const char* path) const
	{
		BString text = fLines.Join("\n");
		text << "\n";
		return write_file(path, text);
	}

	bool GetValue(const char* section, const char* key, BString& value) const
	{
		int32 index = _Find(section, key);
		if (index < 0)
			return false;
		const BString& line = fLines.StringAt(index);
		line.CopyInto(value, line.FindFirst('=') + 1, line.Length());
		value.Trim();
		return true;
	}

	BString Value(const char* section, const char* key) const
	{
		BString value;
		GetValue(section, key, value);
		return value;
	}

	// The keys of a section, in order.
	void Keys(const char* section, BStringList& keys) const
	{
		bool inSection = false;
		for (int32 i = 0; i < fLines.CountStrings(); i++) {
			BString line = fLines.StringAt(i);
			line.Trim();
			if (line.StartsWith("[")) {
				inSection = _IsSection(line, section);
				continue;
			}
			int32 equals = line.FindFirst('=');
			if (!inSection || line.StartsWith("#") || line.StartsWith(";")
				|| equals <= 0)
				continue;
			BString key;
			line.CopyInto(key, 0, equals);
			key.Trim();
			keys.Add(key);
		}
	}

	void SetValue(const char* section, const char* key, const char* value)
	{
		BString line;
		line << key << "=" << value;

		int32 index = _Find(section, key);
		if (index >= 0) {
			fLines.Replace(index, line);
			return;
		}

		// at the end of the section, or a new section at the end
		int32 sectionEnd = -1;
		bool inSection = false;
		for (int32 i = 0; i < fLines.CountStrings(); i++) {
			BString trimmed = fLines.StringAt(i);
			trimmed.Trim();
			if (trimmed.StartsWith("[")) {
				if (inSection)
					break;
				inSection = _IsSection(trimmed, section);
			}
			if (inSection && !trimmed.IsEmpty())
				sectionEnd = i;
		}
		if (sectionEnd >= 0) {
			fLines.Add(line, sectionEnd + 1);
			return;
		}

		if (fLines.CountStrings() > 0
			&& !fLines.StringAt(fLines.CountStrings() - 1).IsEmpty())
			fLines.Add("");
		BString header;
		header << "[" << section << "]";
		fLines.Add(header);
		fLines.Add(line);
	}

	void AddLine(const char* line) { fLines.Add(line); }

private:
	static bool _IsSection(const BString& line, const char* section)
	{
		BString name;
		line.CopyInto(name, 1, line.FindFirst(']') - 1);
		name.Trim();
		return name == section;
	}

	int32 _Find(const char* section, const char* key) const
	{
		bool inSection = false;
		for (int32 i = 0; i < fLines.CountStrings(); i++) {
			BString line = fLines.StringAt(i);
			line.Trim();
			if (line.StartsWith("[")) {
				inSection = _IsSection(line, section);
				continue;
			}
			int32 equals = line.FindFirst('=');
			if (!inSection || equals <= 0 || line.StartsWith("#"))
				continue;
			BString name;
			line.CopyInto(name, 0, equals);
			name.Trim();
			if (name == key)
				return i;
		}
		return -1;
	}

	BStringList fLines;
};


//	#pragma mark - helpers


static bool
parse_color(const BString& text, rgb_color& color)
{
	unsigned int r, g, b;
	if (sscanf(text.String(), "#%02x%02x%02x", &r, &g, &b) != 3)
		return false;
	color = make_color(r, g, b, 255);
	return true;
}


static BString
format_color(rgb_color color)
{
	BString text;
	text.SetToFormat("#%02x%02x%02x", color.red, color.green, color.blue);
	return text;
}


static bool
parse_font(const BString& text, Theme::Font& font)
{
	BStringList parts;
	text.Split(",", false, parts);
	if (parts.CountStrings() != 3)
		return false;
	font.family = parts.StringAt(0);
	font.style = parts.StringAt(1);
	font.family.Trim();
	font.style.Trim();
	font.size = atof(parts.StringAt(2).String());
	font.set = !font.family.IsEmpty() && font.size > 0;
	return font.set;
}


static void
default_font(int32 which, Theme::Font& font)
{
	font.set = true;
	font.size = DEFAULT_FONT_SIZE;
	switch (which) {
		case Theme::kBoldFont:
			font.family = DEFAULT_BOLD_FONT_FAMILY;
			font.style = DEFAULT_BOLD_FONT_STYLE;
			break;
		case Theme::kFixedFont:
			font.family = DEFAULT_FIXED_FONT_FAMILY;
			font.style = DEFAULT_FIXED_FONT_STYLE;
			break;
		default:
			font.family = DEFAULT_PLAIN_FONT_FAMILY;
			font.style = DEFAULT_PLAIN_FONT_STYLE;
			break;
	}
}


static void
current_font(int32 which, Theme::Font& font)
{
	BFont current;
	if (which == Theme::kMenuFont) {
		menu_info info;
		get_menu_info(&info);
		current.SetFamilyAndStyle(info.f_family, info.f_style);
		current.SetSize(info.font_size);
	} else if (which == Theme::kBoldFont)
		current = *be_bold_font;
	else if (which == Theme::kFixedFont)
		current = *be_fixed_font;
	else
		current = *be_plain_font;

	font_family family;
	font_style style;
	current.GetFamilyAndStyle(&family, &style);
	font.set = true;
	font.family = family;
	font.style = style;
	font.size = current.Size();
}


/*!	The font as a Pango description, for labwc's window.decorator.font: the
	window tabs labwc draws round other apps take the bold font, as Hokku's
	own do (describe_font() in InterfaceDefs.cpp).
*/
static BString
pango_font(const Theme::Font& font)
{
	// Pango doesn't know "Regular" as a style
	const char* style = font.style.String();
	const bool regular = strcasecmp(style, "Regular") == 0
		|| strcasecmp(style, "Roman") == 0 || strcasecmp(style, "Book") == 0;
	BString text;
	text.SetToFormat("%s%s%s %gpx", font.family.String(), regular ? "" : " ",
		regular ? "" : style, font.size);
	return text;
}


static void
set_font(int32 which, const Theme::Font& font)
{
	font_family family;
	font_style style;
	strlcpy(family, font.family.String(), sizeof(family));
	strlcpy(style, font.style.String(), sizeof(style));

	// the menu font is part of the menu settings, as in Appearance
	if (which == Theme::kMenuFont) {
		menu_info info;
		if (get_menu_info(&info) != B_OK)
			return;
		strlcpy(info.f_family, family, sizeof(info.f_family));
		strlcpy(info.f_style, style, sizeof(info.f_style));
		info.font_size = font.size;
		set_menu_info(&info);
	} else
		_set_system_font_(kFontNames[which], family, style, font.size);
}


/*!	Finds a control look add-on by file name, user folders first. */
static bool
find_control_look(const BString& name, BString& path)
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
		candidate.Append(name.String());
		if (BEntry(candidate.Path()).Exists()) {
			path = candidate.Path();
			return true;
		}
	}
	return false;
}


static status_t
copy_file(const char* from, const char* to)
{
	BFile source(from, B_READ_ONLY);
	BFile target(to, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status_t status = source.InitCheck();
	if (status == B_OK)
		status = target.InitCheck();
	if (status != B_OK)
		return status;

	char buffer[65536];
	ssize_t bytes;
	while ((bytes = source.Read(buffer, sizeof(buffer))) > 0) {
		if (target.Write(buffer, bytes) != bytes)
			return B_IO_ERROR;
	}
	return bytes < 0 ? (status_t)bytes : B_OK;
}


/*!	A folder under the XDG config home (~/.config), created when needed. */
static BPath
config_path(const char* leaf, bool createParent)
{
	const char* home = getenv("XDG_CONFIG_HOME");
	BPath path;
	if (home != NULL && home[0] == '/')
		path.SetTo(home);
	else {
		path.SetTo(getenv("HOME"));
		path.Append(".config");
	}
	path.Append(leaf);

	if (createParent) {
		BPath parent;
		path.GetParent(&parent);
		create_directory(parent.Path(), 0755);
	}
	return path;
}


static bool
in_path(const char* program)
{
	const char* pathVariable = getenv("PATH");
	BStringList folders;
	BString(pathVariable != NULL ? pathVariable : "/usr/bin").Split(":", true,
		folders);
	for (int32 i = 0; i < folders.CountStrings(); i++) {
		BPath path(folders.StringAt(i).String(), program);
		if (access(path.Path(), X_OK) == 0)
			return true;
	}
	return false;
}


static void
run(const char* const argv[])
{
	pid_t pid;
	if (posix_spawnp(&pid, argv[0], NULL, NULL, (char* const*)argv, environ)
			== 0)
		waitpid(pid, NULL, 0);
}


static void
set_gsetting(const char* key, const BString& value)
{
	if (value.IsEmpty() || !in_path("gsettings"))
		return;
	const char* argv[] = { "gsettings", "set", "org.gnome.desktop.interface",
		key, value.String(), NULL };
	run(argv);
}


//	#pragma mark - Theme


Theme::Theme()
	:
	hasDesktopColor(false),
	desktopColor(make_color(51, 102, 152, 255)),
	fUserTheme(false)
{
	for (int32 i = 0; i < kFontCount; i++) {
		fonts[i].set = false;
		fonts[i].size = 0;
	}
}


status_t
Theme::Load(const BPath& folder)
{
	BPath path(folder.Path(), kThemeFile);
	IniFile ini;
	status_t status = ini.Load(path.Path());
	if (status != B_OK)
		return status;

	fFolder = folder;
	BPath userFolder;
	fUserTheme = user_themes_folder(userFolder) == B_OK
		&& BString(folder.Path()).StartsWith(userFolder.Path());

	name = ini.Value("Theme", "Name");
	if (name.IsEmpty())
		name = folder.Leaf();
	description = ini.Value("Theme", "Description");

	if (!ini.GetValue("Window", "Decorator", decorator))
		decorator = "Default";
	controlLook = ini.Value("Window", "ControlLook");

	colors.MakeEmpty();
	BStringList keys;
	ini.Keys("Colors", keys);
	for (int32 i = 0; i < keys.CountStrings(); i++) {
		rgb_color color;
		const BString& key = keys.StringAt(i);
		if (which_ui_color(key.String()) != (color_which)-1
			&& parse_color(ini.Value("Colors", key.String()), color))
			colors.AddColor(key.String(), color);
	}

	for (int32 i = 0; i < kFontCount; i++) {
		fonts[i].set = false;
		BString value;
		if (ini.GetValue("Fonts", kFontKeys[i], value))
			parse_font(value, fonts[i]);
	}

	BString value;
	hasDesktopColor = ini.GetValue("Desktop", "Color", value)
		&& parse_color(value, desktopColor);
	wallpaper = ini.Value("Desktop", "Wallpaper");
	wallpaperMode = ini.Value("Desktop", "WallpaperMode");

	labwc = ini.Value("Linux", "Labwc");
	gtkTheme = ini.Value("Linux", "GtkTheme");
	iconTheme = ini.Value("Linux", "IconTheme");
	cursorTheme = ini.Value("Linux", "CursorTheme");
	qtStyle = ini.Value("Linux", "QtStyle");
	kvantumTheme = ini.Value("Linux", "KvantumTheme");
	return B_OK;
}


status_t
Theme::Save(const BPath& folder) const
{
	status_t status = create_directory(folder.Path(), 0755);
	if (status != B_OK)
		return status;

	IniFile ini;
	ini.AddLine("# A Hokku theme: see Appearance's Themes tab");
	ini.SetValue("Theme", "Name", name.String());
	if (!description.IsEmpty())
		ini.SetValue("Theme", "Description", description.String());

	ini.SetValue("Window", "Decorator", decorator.String());
	ini.SetValue("Window", "ControlLook", controlLook.String());

	char* key;
	type_code type;
	for (int32 i = 0; colors.GetInfo(B_RGB_32_BIT_TYPE, i, &key, &type) == B_OK;
			i++) {
		rgb_color color = colors.GetColor(key, make_color(0, 0, 0));
		ini.SetValue("Colors", key, format_color(color).String());
	}

	for (int32 i = 0; i < kFontCount; i++) {
		if (!fonts[i].set)
			continue;
		BString value;
		value.SetToFormat("%s, %s, %g", fonts[i].family.String(),
			fonts[i].style.String(), fonts[i].size);
		ini.SetValue("Fonts", kFontKeys[i], value.String());
	}

	if (hasDesktopColor)
		ini.SetValue("Desktop", "Color", format_color(desktopColor).String());
	ini.SetValue("Desktop", "Wallpaper", wallpaper.String());
	if (!wallpaperMode.IsEmpty())
		ini.SetValue("Desktop", "WallpaperMode", wallpaperMode.String());

	// labwc's frames go with the theme; the wallpaper is referred to
	BString labwcFile = labwc;
	if (labwc.StartsWith("/")) {
		labwcFile = "labwc-themerc";
		BPath target(folder.Path(), labwcFile.String());
		if (copy_file(labwc.String(), target.Path()) != B_OK)
			labwcFile = "";
	}

	const struct { const char* key; const BString* value; } linux[] = {
		{ "Labwc", &labwcFile },
		{ "GtkTheme", &gtkTheme },
		{ "IconTheme", &iconTheme },
		{ "CursorTheme", &cursorTheme },
		{ "QtStyle", &qtStyle },
		{ "KvantumTheme", &kvantumTheme },
	};
	for (size_t i = 0; i < sizeof(linux) / sizeof(linux[0]); i++) {
		if (!linux[i].value->IsEmpty())
			ini.SetValue("Linux", linux[i].key, linux[i].value->String());
	}

	BPath path(folder.Path(), kThemeFile);
	return ini.Save(path.Path());
}


/*!	What is in use now. The wallpaper and labwc's frames are copied into the
	theme's folder when it is saved, so it holds them while they stay where
	they are; Save() doesn't copy, Capture() leaves absolute paths.
*/
status_t
Theme::Capture(const char* themeName)
{
	name = themeName;
	description = "";

	if (!BPrivate::get_decorator(decorator))
		decorator = "Default";
	BString path;
	BPrivate::get_control_look(path);
	controlLook = path.IsEmpty() ? "" : BPath(path.String()).Leaf();

	// only the colours that differ from Haiku's
	colors.MakeEmpty();
	for (int32 index = 0; index < kColorWhichCount; index++) {
		color_which which = index_to_color_which(index);
		if (which > B_FAILURE_COLOR)
			continue;	// the table's spare last slot
		rgb_color color = ui_color(which);
		if (color != BPrivate::kDefaultColors[index])
			colors.AddColor(ui_color_name(which), color);
	}

	for (int32 i = 0; i < kFontCount; i++)
		current_font(i, fonts[i]);

	hasDesktopColor = true;
	desktopColor = BScreen().DesktopColor();

	wallpaper = "";
	wallpaperMode = "";
	BPath desktop;
	if (find_directory(B_DESKTOP_DIRECTORY, &desktop) == B_OK) {
		BNode node(desktop.Path());
		BMessage info;
		attr_info attrInfo;
		if (node.GetAttrInfo(kBackgroundInfo, &attrInfo) == B_OK) {
			char* buffer = new char[attrInfo.size];
			if (node.ReadAttr(kBackgroundInfo, B_MESSAGE_TYPE, 0, buffer,
					attrInfo.size) == attrInfo.size
				&& info.Unflatten(buffer) == B_OK) {
				wallpaper = info.GetString("be:bgndimginfopath", "");
				switch (info.GetInt32("be:bgndimginfomode", kWallpaperScaled)) {
					case kWallpaperCentered: wallpaperMode = "centered"; break;
					case kWallpaperTiled: wallpaperMode = "tiled"; break;
					default: wallpaperMode = "scaled"; break;
				}
			}
			delete[] buffer;
		}
	}

	labwc = "";
	BPath override = config_path(kLabwcOverride, false);
	if (BEntry(override.Path()).Exists())
		labwc = override.Path();

	IniFile gtk;
	if (gtk.Load(config_path("gtk-3.0/settings.ini", false).Path()) == B_OK) {
		gtkTheme = gtk.Value("Settings", "gtk-theme-name");
		iconTheme = gtk.Value("Settings", "gtk-icon-theme-name");
		cursorTheme = gtk.Value("Settings", "gtk-cursor-theme-name");
	}
	IniFile qt;
	if (qt.Load(config_path("qt6ct/qt6ct.conf", false).Path()) == B_OK)
		qtStyle = qt.Value("Appearance", "style");
	IniFile kvantum;
	if (kvantum.Load(config_path("Kvantum/kvantum.kvconfig", false).Path())
			== B_OK)
		kvantumTheme = kvantum.Value("General", "theme");

	fUserTheme = true;
	return B_OK;
}


BPath
Theme::_Resolve(const BString& file) const
{
	if (file.IsEmpty())
		return BPath();
	if (file.StartsWith("/"))
		return BPath(file.String());
	return BPath(fFolder.Path(), file.String());
}


status_t
Theme::Apply() const
{
	status_t result = BPrivate::set_decorator(decorator);
	if (result != B_OK) {
		fprintf(stderr, "Theme: no \"%s\" decorator\n", decorator.String());
		BPrivate::set_decorator("Default");
	}

	BString controlLookPath;
	if (!controlLook.IsEmpty()
		&& !find_control_look(controlLook, controlLookPath)) {
		fprintf(stderr, "Theme: the control look %s isn't installed\n",
			controlLook.String());
	}
	BPrivate::set_control_look(controlLookPath);

	// Haiku's colours, with the theme's over them
	BMessage all;
	for (int32 index = 0; index < kColorWhichCount; index++) {
		color_which which = index_to_color_which(index);
		if (which > B_FAILURE_COLOR)
			continue;	// the table's spare last slot
		const char* key = ui_color_name(which);
		all.AddColor(key, colors.GetColor(key,
			BPrivate::kDefaultColors[index]));
	}
	set_ui_colors(&all);

	for (int32 i = 0; i < kFontCount; i++) {
		Font font = fonts[i];
		if (!font.set)
			default_font(i, font);
		set_font(i, font);
	}

	BScreen().SetDesktopColor(hasDesktopColor ? desktopColor
		: make_color(51, 102, 152, 255), true);

	// the wallpaper: Tracker's background setting on the Desktop folder
	BPath desktop;
	if (find_directory(B_DESKTOP_DIRECTORY, &desktop, true) == B_OK) {
		BMessage info;
		int32 mode = kWallpaperScaled;
		if (wallpaperMode == "centered")
			mode = kWallpaperCentered;
		else if (wallpaperMode == "tiled")
			mode = kWallpaperTiled;
		info.AddBool("be:bgndimginfoerasetext", true);
		info.AddString("be:bgndimginfopath", wallpaper.IsEmpty() ? ""
			: _Resolve(wallpaper).Path());
		info.AddInt32("be:bgndimginfoworkspaces", (int32)B_ALL_WORKSPACES);
		info.AddPoint("be:bgndimginfooffset", BPoint(0, 0));
		info.AddInt32("be:bgndimginfomode", mode);
		info.AddInt32("be:bgndimginfoset", 0);

		ssize_t size = info.FlattenedSize();
		char* buffer = new char[size];
		if (info.Flatten(buffer, size) == B_OK) {
			BNode(desktop.Path()).WriteAttr(kBackgroundInfo, B_MESSAGE_TYPE, 0,
				buffer, size);
		}
		delete[] buffer;
		BMessenger(kTrackerSignature).SendMessage(B_RESTORE_BACKGROUND_IMAGE);
	}

	// labwc's frames round other apps: the theme's themerc over labwc's
	// theme, which labwc reloads on SIGHUP. A themerc-override that isn't
	// ours is the user's own, kept as themerc-override.orig.
	BPath override = config_path(kLabwcOverride, !labwc.IsEmpty());
	BString existing;
	bool ours = read_file(override.Path(), existing) != B_OK
		|| existing.StartsWith(kLabwcHeader);
	if (!labwc.IsEmpty()) {
		BString themerc;
		BPath source = _Resolve(labwc);
		if (read_file(source.Path(), themerc) == B_OK) {
			if (!ours) {
				BString backup(override.Path());
				backup << ".orig";
				rename(override.Path(), backup.String());
			}
			// a captured theme's file may be an override we wrote
			if (themerc.StartsWith(kLabwcHeader))
				themerc.Remove(0, themerc.FindFirst('\n') + 1);
			BString text(kLabwcHeader);
			text << " (" << name << "); applying a theme replaces it\n";

			// The tabs labwc draws (window.decorator, in Hokku OS's labwc)
			// are titled in the theme's bold font, which a captured file's
			// line may predate.
			BStringList lines;
			themerc.Split("\n", false, lines);
			for (int32 i = 0; i < lines.CountStrings(); i++) {
				if (!lines.StringAt(i).StartsWith("window.decorator.font"))
					text << lines.StringAt(i) << "\n";
			}
			Font bold = fonts[kBoldFont];
			if (!bold.set)
				default_font(kBoldFont, bold);
			text << "window.decorator.font: " << pango_font(bold) << "\n";
			write_file(override.Path(), text);
		} else
			fprintf(stderr, "Theme: can't read %s\n", source.Path());
	} else if (ours)
		unlink(override.Path());

	const char* labwcPid = getenv("LABWC_PID");
	if (labwcPid != NULL && atoi(labwcPid) > 0)
		kill(atoi(labwcPid), SIGHUP);

	// GTK reads settings.ini at startup; running GTK apps follow gsettings.
	// Its font is the plain font: Be sizes are pixels at 72 dpi, GTK's
	// points at 96, so 12 is 9.
	Font plain = fonts[kPlainFont];
	if (!plain.set)
		default_font(kPlainFont, plain);
	BString gtkFont(plain.family);
	if (plain.style != "Regular")
		gtkFont << " " << plain.style;
	BString size;
	size.SetToFormat(" %g", plain.size * 0.75f);
	gtkFont << size;

	const char* gtkFiles[] = { "gtk-3.0/settings.ini", "gtk-4.0/settings.ini" };
	for (size_t i = 0; i < sizeof(gtkFiles) / sizeof(gtkFiles[0]); i++) {
		BPath path = config_path(gtkFiles[i], true);
		IniFile gtk;
		gtk.Load(path.Path());
		gtk.SetValue("Settings", "gtk-font-name", gtkFont.String());
		if (!gtkTheme.IsEmpty())
			gtk.SetValue("Settings", "gtk-theme-name", gtkTheme.String());
		if (!iconTheme.IsEmpty())
			gtk.SetValue("Settings", "gtk-icon-theme-name", iconTheme.String());
		if (!cursorTheme.IsEmpty()) {
			gtk.SetValue("Settings", "gtk-cursor-theme-name",
				cursorTheme.String());
		}
		gtk.Save(path.Path());
	}
	set_gsetting("font-name", gtkFont);
	set_gsetting("gtk-theme", gtkTheme);
	set_gsetting("icon-theme", iconTheme);
	set_gsetting("cursor-theme", cursorTheme);

	// Qt: qt5ct's and qt6ct's settings (the session picks the platform
	// theme), and Kvantum's theme
	const char* qtFiles[] = { "qt5ct/qt5ct.conf", "qt6ct/qt6ct.conf" };
	for (size_t i = 0; i < sizeof(qtFiles) / sizeof(qtFiles[0]); i++) {
		if (qtStyle.IsEmpty() && iconTheme.IsEmpty())
			break;
		BPath path = config_path(qtFiles[i], true);
		IniFile qt;
		qt.Load(path.Path());
		if (!qtStyle.IsEmpty())
			qt.SetValue("Appearance", "style", qtStyle.String());
		if (!iconTheme.IsEmpty())
			qt.SetValue("Appearance", "icon_theme", iconTheme.String());
		qt.Save(path.Path());
	}
	if (!kvantumTheme.IsEmpty()) {
		BPath path = config_path("Kvantum/kvantum.kvconfig", true);
		IniFile kvantum;
		kvantum.Load(path.Path());
		kvantum.SetValue("General", "theme", kvantumTheme.String());
		kvantum.Save(path.Path());
	}

	set_current_theme_name(name.String());
	return B_OK;
}


//	#pragma mark - finding themes


status_t
user_themes_folder(BPath& path, bool create)
{
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path, create);
	if (status != B_OK)
		return status;
	path.Append(kThemesFolder);
	if (create)
		create_directory(path.Path(), 0755);
	return B_OK;
}


static int
compare_themes(const Theme* a, const Theme* b)
{
	return a->name.ICompare(b->name);
}


status_t
find_themes(ThemeList& themes)
{
	themes.MakeEmpty();

	BPath folders[2];
	user_themes_folder(folders[0]);
	if (find_directory(B_SYSTEM_DATA_DIRECTORY, &folders[1]) == B_OK)
		folders[1].Append(kThemesFolder);

	for (int32 f = 0; f < 2; f++) {
		BDirectory directory(folders[f].Path());
		BEntry entry;
		while (directory.GetNextEntry(&entry, true) == B_OK) {
			if (!entry.IsDirectory())
				continue;
			BPath path;
			entry.GetPath(&path);
			Theme* theme = new Theme;
			bool known = false;
			if (theme->Load(path) == B_OK) {
				for (int32 i = 0; i < themes.CountItems() && !known; i++)
					known = themes.ItemAt(i)->name == theme->name;
			}
			if (known || theme->name.IsEmpty() || theme->Folder().InitCheck()
					!= B_OK)
				delete theme;
			else
				themes.AddItem(theme);
		}
	}

	themes.SortItems(compare_themes);
	return B_OK;
}


BString
current_theme_name()
{
	BPath path;
	BString name;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return name;
	path.Append(kCurrentThemeFile);

	BFile file(path.Path(), B_READ_ONLY);
	char buffer[B_FILE_NAME_LENGTH] = {};
	if (file.Read(buffer, sizeof(buffer) - 1) > 0)
		name = buffer;
	name.Trim();
	return name;
}


status_t
set_current_theme_name(const char* name)
{
	BPath path;
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path, true);
	if (status != B_OK)
		return status;
	path.Append(kCurrentThemeFile);

	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	BString text(name);
	text << "\n";
	return file.Write(text.String(), text.Length()) == text.Length()
		? B_OK : B_IO_ERROR;
}
