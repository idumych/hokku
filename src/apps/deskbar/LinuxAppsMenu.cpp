/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 */


#include "LinuxAppsMenu.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include <gio/gdesktopappinfo.h>
#include <gio/gio.h>

#include <Application.h>
#include <Bitmap.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <Mime.h>

#include "BitmapMenuItem.h"
#include "IconLoader.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "LinuxAppsMenu"


namespace {

struct Category {
	const char*	id;			// freedesktop main category
	const char*	label;		// as the Plasma and Xfce menus call it
	const char*	icon;
};

const Category kCategories[] = {
	{ "AudioVideo", B_TRANSLATE_MARK("Multimedia"), "applications-multimedia" },
	{ "Audio", B_TRANSLATE_MARK("Multimedia"), "applications-multimedia" },
	{ "Video", B_TRANSLATE_MARK("Multimedia"), "applications-multimedia" },
	{ "Development", B_TRANSLATE_MARK("Development"),
		"applications-development" },
	{ "Education", B_TRANSLATE_MARK("Education"), "applications-education" },
	{ "Game", B_TRANSLATE_MARK("Games"), "applications-games" },
	{ "Graphics", B_TRANSLATE_MARK("Graphics"), "applications-graphics" },
	{ "Network", B_TRANSLATE_MARK("Internet"), "applications-internet" },
	{ "Office", B_TRANSLATE_MARK("Office"), "applications-office" },
	{ "Science", B_TRANSLATE_MARK("Science"), "applications-science" },
	{ "Settings", B_TRANSLATE_MARK("Settings"), "preferences-system" },
	{ "System", B_TRANSLATE_MARK("System"), "applications-system" },
	{ "Utility", B_TRANSLATE_MARK("Utilities"), "applications-utilities" },
};
const Category kOther = { NULL, B_TRANSLATE_MARK("Other"),
	"applications-other" };


struct App {
	std::string	name;
	std::string	path;		// the .desktop file
	std::string	icon;		// icon names, or a file, one per line
};


/*!	What the last scan found. The Deskbar menu is built anew each time it
	opens, so this outlives the menus; it is scanned again only when one of
	the applications folders changed.
*/
struct Cache {
	std::string									stamp;
	std::map<const Category*, std::vector<App> >	apps;
	std::map<std::string, BBitmap*>				icons;
};

Cache sCache;


std::vector<std::string>
application_dirs()
{
	std::vector<std::string> dirs;
	dirs.push_back(std::string(g_get_user_data_dir()) + "/applications");
	for (const gchar* const* dir = g_get_system_data_dirs(); *dir != NULL;
			dir++) {
		dirs.push_back(std::string(*dir) + "/applications");
	}
	return dirs;
}


/*!	Finds the .desktop files under dir. A file's desktop ID is its path
	below the applications folder with '/' as '-'; the first folder to have
	an ID wins, so the user's files override the system's (or, hidden, hide
	them). stamp records every folder's modification time.
*/
void
scan(const std::string& dir, const std::string& prefix,
	std::map<std::string, std::string>& files, std::string& stamp)
{
	struct stat info;
	if (stat(dir.c_str(), &info) != 0 || !S_ISDIR(info.st_mode))
		return;
	char time[64];
	snprintf(time, sizeof(time), " %lld.%ld\n", (long long)info.st_mtim.tv_sec,
		info.st_mtim.tv_nsec);
	stamp += dir + time;

	DIR* handle = opendir(dir.c_str());
	if (handle == NULL)
		return;
	while (struct dirent* entry = readdir(handle)) {
		if (entry->d_name[0] == '.')
			continue;
		std::string path = dir + "/" + entry->d_name;
		struct stat entryInfo;
		if (stat(path.c_str(), &entryInfo) != 0)
			continue;
		if (S_ISDIR(entryInfo.st_mode)) {
			scan(path, prefix + entry->d_name + "-", files, stamp);
			continue;
		}
		size_t length = strlen(entry->d_name);
		if (length > 8 && strcmp(entry->d_name + length - 8, ".desktop") == 0)
			files.insert(std::make_pair(prefix + entry->d_name, path));
	}
	closedir(handle);
}


const Category*
main_category(const char* categories)
{
	if (categories == NULL)
		return &kOther;

	gchar** list = g_strsplit(categories, ";", -1);
	const Category* found = &kOther;
	for (gchar** entry = list; *entry != NULL && found == &kOther; entry++) {
		for (const Category& category : kCategories) {
			if (strcmp(*entry, category.id) == 0) {
				found = &category;
				break;
			}
		}
	}
	g_strfreev(list);
	return found;
}


std::string
icon_names(GIcon* icon)
{
	if (G_IS_FILE_ICON(icon)) {
		gchar* path = g_file_get_path(g_file_icon_get_file(G_FILE_ICON(icon)));
		std::string result = path != NULL ? path : "";
		g_free(path);
		return result;
	}

	std::string result;
	if (G_IS_THEMED_ICON(icon)) {
		const gchar* const* names = g_themed_icon_get_names(
			G_THEMED_ICON(icon));
		for (; names != NULL && *names != NULL; names++)
			result += std::string(*names) + "\n";
	}
	return result;
}


/*!	Scans the applications folders and reads what changed since last time.
*/
void
refresh()
{
	std::map<std::string, std::string> files;
	std::string stamp;
	for (const std::string& dir : application_dirs())
		scan(dir, "", files, stamp);
	if (stamp == sCache.stamp)
		return;

	sCache.stamp = stamp;
	sCache.apps.clear();
	for (auto& file : files) {
		// NULL for files that aren't applications or whose TryExec isn't
		// installed
		GDesktopAppInfo* info
			= g_desktop_app_info_new_from_filename(file.second.c_str());
		if (info == NULL)
			continue;

		// Hidden, NoDisplay, and OnlyShowIn/NotShowIn for this desktop
		GAppInfo* appInfo = G_APP_INFO(info);
		if (!g_desktop_app_info_get_is_hidden(info)
			&& g_app_info_should_show(appInfo)) {
			App app;
			app.name = g_app_info_get_name(appInfo);
			app.path = file.second;
			GIcon* icon = g_app_info_get_icon(appInfo);
			if (icon != NULL)
				app.icon = icon_names(icon);
			const Category* category = main_category(
				g_desktop_app_info_get_categories(info));
			sCache.apps[category].push_back(app);
		}
		g_object_unref(info);
	}

	for (auto& category : sCache.apps) {
		std::sort(category.second.begin(), category.second.end(),
			[](const App& a, const App& b) {
				return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
			});
	}
}


/*!	The icon for a menu item, by icon names or file, at the size of the
	Deskbar's other menu icons (drawn at twice that, for HiDPI). Icons are
	loaded once; the caller gets its own copy.
*/
BBitmap*
menu_icon(const std::string& names)
{
	BSize size = be_control_look->ComposeIconSize(B_MINI_ICON);
	int pixels = (size.IntegerWidth() + 1) * 2;
	std::string key = names + "@" + std::to_string(pixels);

	auto found = sCache.icons.find(key);
	if (found == sCache.icons.end()) {
		cairo_surface_t* surface = NULL;
		if (names[0] == '/')
			surface = IconLoader::LoadFile(names.c_str(), pixels);
		else {
			size_t start = 0;
			while (surface == NULL && start < names.size()) {
				size_t end = names.find('\n', start);
				if (end == std::string::npos)
					end = names.size();
				std::string name = names.substr(start, end - start);
				surface = IconLoader::LoadNamed(name.c_str(), pixels);
				start = end + 1;
			}
		}
		if (surface == NULL)
			surface = IconLoader::LoadNamed("application-x-executable", pixels);

		BBitmap* icon = NULL;
		if (surface != NULL) {
			BBitmap* bitmap = IconLoader::MakeBitmap(surface, pixels);
			cairo_surface_destroy(surface);
			if (bitmap != NULL) {
				icon = new BBitmap(bitmap->Bounds(), B_BITMAP_HIDPI,
					B_RGBA32);
				icon->ImportBits(bitmap);
				delete bitmap;
			}
		}
		found = sCache.icons.insert(std::make_pair(key, icon)).first;
	}

	if (found->second == NULL)
		return NULL;
	BBitmap* copy = new BBitmap(found->second->Bounds(), B_BITMAP_HIDPI,
		B_RGBA32);
	copy->ImportBits(found->second);
	return copy;
}


/*!	One category's applications; their icons are loaded when it opens. */
class TLinuxCategoryMenu : public BMenu {
public:
	TLinuxCategoryMenu(const char* label, const std::vector<App>& apps)
		:
		BMenu(label),
		fApps(apps),
		fBuilt(false)
	{
	}

	virtual void AttachedToWindow()
	{
		if (!fBuilt) {
			fBuilt = true;
			for (const App& app : fApps) {
				BMessage* message = new BMessage(kLaunchLinuxApp);
				message->AddString("path", app.path.c_str());
				BMenuItem* item = new TBitmapMenuItem(app.name.c_str(),
					message, app.icon.empty() ? menu_icon(
						"application-x-executable") : menu_icon(app.icon));
				item->SetTarget(be_app);
				AddItem(item);
			}
		}
		BMenu::AttachedToWindow();
	}

private:
	std::vector<App>	fApps;
	bool				fBuilt;
};


bool
points_into(const char* path, const char* directory)
{
	size_t length = directory != NULL ? strlen(directory) : 0;
	return path != NULL && length > 0 && strncmp(path, directory, length) == 0;
}

}	// namespace


TLinuxAppsMenu::TLinuxAppsMenu()
	:
	BMenu(B_TRANSLATE("Linux applications")),
	fBuilt(false)
{
}


BMenuItem*
TLinuxAppsMenu::NewItem()
{
	return new TBitmapMenuItem(new TLinuxAppsMenu(),
		menu_icon("start-here-kde\nstart-here\napplications-other"));
}


void
TLinuxAppsMenu::AttachedToWindow()
{
	if (!fBuilt) {
		fBuilt = true;
		_Build();
	}
	BMenu::AttachedToWindow();
}


void
TLinuxAppsMenu::_Build()
{
	refresh();

	std::vector<std::pair<const Category*, std::string> > categories;
	for (auto& category : sCache.apps) {
		if (category.first != &kOther) {
			categories.push_back(std::make_pair(category.first,
				std::string(B_TRANSLATE_NOCOLLECT(category.first->label))));
		}
	}
	std::sort(categories.begin(), categories.end(),
		[](const std::pair<const Category*, std::string>& a,
			const std::pair<const Category*, std::string>& b) {
			return strcasecmp(a.second.c_str(), b.second.c_str()) < 0;
		});
	if (sCache.apps.find(&kOther) != sCache.apps.end()) {
		categories.push_back(std::make_pair(&kOther,
			std::string(B_TRANSLATE_NOCOLLECT(kOther.label))));
	}

	for (auto& category : categories) {
		// Multimedia comes from three categories but is one submenu
		bool merged = false;
		for (int32 i = 0; i < CountItems(); i++) {
			if (category.second == ItemAt(i)->Label()) {
				merged = true;
				break;
			}
		}
		if (merged)
			continue;

		std::vector<App> apps;
		for (auto& entry : sCache.apps) {
			if (B_TRANSLATE_NOCOLLECT(entry.first->label) == category.second)
				apps.insert(apps.end(), entry.second.begin(), entry.second.end());
		}
		std::sort(apps.begin(), apps.end(), [](const App& a, const App& b) {
			return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
		});

		AddItem(new TBitmapMenuItem(
			new TLinuxCategoryMenu(category.second.c_str(), apps),
			menu_icon(category.first->icon)));
	}

	if (CountItems() == 0) {
		BMenuItem* item = new BMenuItem(B_TRANSLATE("<none found>"), NULL);
		item->SetEnabled(false);
		AddItem(item);
	}
}


/*!	Starts the app as the desktop would. Like BRoster::Launch() with
	xdg-open, it gets the desktop's environment, without what an AppImage's
	AppRun set up for its bundle.
*/
void
TLinuxAppsMenu::LaunchLinuxApp(const char* desktopFile)
{
	if (desktopFile == NULL)
		return;

	GDesktopAppInfo* info = g_desktop_app_info_new_from_filename(desktopFile);
	if (info == NULL) {
		fprintf(stderr, "Deskbar: can't read %s\n", desktopFile);
		return;
	}

	GAppLaunchContext* context = g_app_launch_context_new();
	g_app_launch_context_unsetenv(context, "COSMOE_ROSTER_LAUNCH");
	const char* appDir = getenv("APPDIR");
	const char* bundleVariables[] = { "FONTCONFIG_FILE", "TRANSLATORS",
		"LD_LIBRARY_PATH" };
	for (const char* variable : bundleVariables) {
		if (points_into(getenv(variable), appDir))
			g_app_launch_context_unsetenv(context, variable);
	}

	GError* error = NULL;
	if (!g_app_info_launch(G_APP_INFO(info), NULL, context, &error)) {
		fprintf(stderr, "Deskbar: couldn't start %s: %s\n", desktopFile,
			error->message);
		g_error_free(error);
	}
	g_object_unref(context);
	g_object_unref(info);
}
