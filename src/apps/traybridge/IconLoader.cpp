/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Icon lookup for tray items, following the freedesktop Icon Theme
 * Specification closely enough for tray icons, and conversion of what was
 * found into BBitmaps.
 */


#include "IconLoader.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <map>
#include <set>
#include <vector>

#include <gio/gio.h>
#ifdef HAVE_LIBRSVG
#	include <librsvg/rsvg.h>
#endif

#include <Bitmap.h>
#include <InterfaceDefs.h>


namespace {


struct ThemeDir {
	std::string	subdir;
	int			size = 0;
	int			scale = 1;
	int			minSize = 0;
	int			maxSize = 0;
	int			threshold = 2;
	enum { kFixed, kScalable, kThreshold } type = kThreshold;
};


struct Theme {
	std::string					name;
	std::vector<std::string>	roots;		// <base>/<name> folders found
	std::vector<ThemeDir>		dirs;
	std::vector<std::string>	inherits;
};


const char* kExtensions[] = {
	".png",
#ifdef HAVE_LIBRSVG
	".svg",
#endif
	NULL
};


#ifdef HAVE_LIBRSVG
/*!	KDE's symbolic icons (KDE Connect's, Breeze's) colour their parts with
	the classes of a "current-color-scheme" stylesheet, which Plasma replaces
	with its own colours when it draws them. Here they get the system
	colours, so they match the Deskbar instead of being drawn for a dark
	panel. The file's own stylesheet is author CSS, so these need !important.
*/
std::string
color_scheme_stylesheet()
{
	struct { const char* name; rgb_color color; } classes[] = {
		{ "Text", ui_color(B_PANEL_TEXT_COLOR) },
		{ "Background", ui_color(B_PANEL_BACKGROUND_COLOR) },
		{ "Highlight", ui_color(B_NAVIGATION_BASE_COLOR) },
		{ "HighlightedText", ui_color(B_DOCUMENT_BACKGROUND_COLOR) },
		{ "ViewText", ui_color(B_DOCUMENT_TEXT_COLOR) },
		{ "ViewBackground", ui_color(B_DOCUMENT_BACKGROUND_COLOR) },
		{ "ViewHover", ui_color(B_NAVIGATION_BASE_COLOR) },
		{ "ViewFocus", ui_color(B_NAVIGATION_BASE_COLOR) },
		{ "ButtonText", ui_color(B_CONTROL_TEXT_COLOR) },
		{ "ButtonBackground", ui_color(B_CONTROL_BACKGROUND_COLOR) },
		{ "ButtonHover", ui_color(B_NAVIGATION_BASE_COLOR) },
		{ "ButtonFocus", ui_color(B_NAVIGATION_BASE_COLOR) },
		{ "NegativeText", ui_color(B_FAILURE_COLOR) },
		{ "PositiveText", ui_color(B_SUCCESS_COLOR) },
		{ "NeutralText", make_color(246, 116, 0) },
	};

	std::string css;
	for (auto& entry : classes) {
		char rule[96];
		snprintf(rule, sizeof(rule),
			".ColorScheme-%s { color: #%02x%02x%02x !important; }\n",
			entry.name, entry.color.red, entry.color.green, entry.color.blue);
		css += rule;
	}
	return css;
}
#endif


std::vector<std::string>
base_dirs()
{
	static std::vector<std::string> sDirs;
	if (!sDirs.empty())
		return sDirs;

	const char* home = g_get_home_dir();
	if (home != NULL)
		sDirs.push_back(std::string(home) + "/.icons");
	sDirs.push_back(std::string(g_get_user_data_dir()) + "/icons");
	for (const gchar* const* dir = g_get_system_data_dirs(); *dir != NULL;
			dir++) {
		sDirs.push_back(std::string(*dir) + "/icons");
	}
	return sDirs;
}


bool
file_exists(const std::string& path)
{
	return g_file_test(path.c_str(), G_FILE_TEST_IS_REGULAR);
}


bool
dir_matches_size(const ThemeDir& dir, int size)
{
	switch (dir.type) {
		case ThemeDir::kFixed:
			return dir.size == size;
		case ThemeDir::kScalable:
			return dir.minSize <= size && size <= dir.maxSize;
		case ThemeDir::kThreshold:
		default:
			return dir.size - dir.threshold <= size
				&& size <= dir.size + dir.threshold;
	}
}


int
dir_size_distance(const ThemeDir& dir, int size)
{
	switch (dir.type) {
		case ThemeDir::kFixed:
			return abs(dir.size - size);
		case ThemeDir::kScalable:
			if (size < dir.minSize)
				return dir.minSize - size;
			if (size > dir.maxSize)
				return size - dir.maxSize;
			return 0;
		case ThemeDir::kThreshold:
		default:
			if (size < dir.size - dir.threshold)
				return dir.size - dir.threshold - size;
			if (size > dir.size + dir.threshold)
				return size - dir.size - dir.threshold;
			return 0;
	}
}


Theme*
load_theme(const std::string& name)
{
	static std::map<std::string, Theme*> sThemes;
	auto found = sThemes.find(name);
	if (found != sThemes.end())
		return found->second;

	Theme* theme = NULL;
	for (const std::string& base : base_dirs()) {
		std::string root = base + "/" + name;
		if (!g_file_test(root.c_str(), G_FILE_TEST_IS_DIR))
			continue;

		if (theme == NULL) {
			std::string index = root + "/index.theme";
			GKeyFile* keyFile = g_key_file_new();
			g_key_file_set_list_separator(keyFile, ',');
				// index.theme lists are comma-separated, not GLib's ';'
			if (!g_key_file_load_from_file(keyFile, index.c_str(),
					G_KEY_FILE_NONE, NULL)) {
				g_key_file_free(keyFile);
				continue;
			}

			theme = new Theme;
			theme->name = name;

			gchar** inherits = g_key_file_get_string_list(keyFile,
				"Icon Theme", "Inherits", NULL, NULL);
			for (gchar** entry = inherits; entry != NULL && *entry != NULL;
					entry++) {
				theme->inherits.push_back(*entry);
			}
			g_strfreev(inherits);

			// Directories plus ScaledDirectories, as they may differ
			const char* listKeys[] = { "Directories", "ScaledDirectories" };
			std::set<std::string> seen;
			for (const char* listKey : listKeys) {
				gchar** dirs = g_key_file_get_string_list(keyFile,
					"Icon Theme", listKey, NULL, NULL);
				for (gchar** entry = dirs; entry != NULL && *entry != NULL;
						entry++) {
					if (!seen.insert(*entry).second
						|| !g_key_file_has_group(keyFile, *entry)) {
						continue;
					}

					ThemeDir dir;
					dir.subdir = *entry;
					dir.size = g_key_file_get_integer(keyFile, *entry, "Size",
						NULL);
					int scale = g_key_file_get_integer(keyFile, *entry,
						"Scale", NULL);
					dir.scale = scale > 0 ? scale : 1;
					dir.minSize = dir.maxSize = dir.size;
					if (g_key_file_has_key(keyFile, *entry, "MinSize", NULL)) {
						dir.minSize = g_key_file_get_integer(keyFile, *entry,
							"MinSize", NULL);
					}
					if (g_key_file_has_key(keyFile, *entry, "MaxSize", NULL)) {
						dir.maxSize = g_key_file_get_integer(keyFile, *entry,
							"MaxSize", NULL);
					}
					if (g_key_file_has_key(keyFile, *entry, "Threshold",
							NULL)) {
						dir.threshold = g_key_file_get_integer(keyFile, *entry,
							"Threshold", NULL);
					}
					gchar* type = g_key_file_get_string(keyFile, *entry,
						"Type", NULL);
					if (type != NULL) {
						if (strcmp(type, "Fixed") == 0)
							dir.type = ThemeDir::kFixed;
						else if (strcmp(type, "Scalable") == 0)
							dir.type = ThemeDir::kScalable;
						g_free(type);
					}
					theme->dirs.push_back(dir);
				}
				g_strfreev(dirs);
			}
			g_key_file_free(keyFile);
		}
		theme->roots.push_back(root);
	}

	sThemes[name] = theme;
	return theme;
}


std::string
user_theme_name()
{
	static std::string sName;
	static bool sLooked = false;
	if (sLooked)
		return sName;
	sLooked = true;

	const char* env = getenv("HOKKU_ICON_THEME");
	if (env != NULL && env[0] != '\0')
		return sName = env;

	std::string config = g_get_user_config_dir();

	// KDE
	GKeyFile* keyFile = g_key_file_new();
	if (g_key_file_load_from_file(keyFile, (config + "/kdeglobals").c_str(),
			G_KEY_FILE_NONE, NULL)) {
		gchar* theme = g_key_file_get_string(keyFile, "Icons", "Theme", NULL);
		if (theme != NULL) {
			sName = theme;
			g_free(theme);
		}
	}
	g_key_file_free(keyFile);
	if (!sName.empty())
		return sName;

	// Plasma leaves the key out while the theme is its default
	const char* desktop = getenv("XDG_CURRENT_DESKTOP");
	if (desktop != NULL && strstr(desktop, "KDE") != NULL)
		return sName = "breeze";

	// GNOME, Xfce and other GTK desktops
	GSettingsSchemaSource* source = g_settings_schema_source_get_default();
	GSettingsSchema* schema = source != NULL
		? g_settings_schema_source_lookup(source,
			"org.gnome.desktop.interface", TRUE)
		: NULL;
	if (schema != NULL) {
		g_settings_schema_unref(schema);
		GSettings* settings = g_settings_new("org.gnome.desktop.interface");
		gchar* theme = g_settings_get_string(settings, "icon-theme");
		if (theme != NULL && theme[0] != '\0')
			sName = theme;
		g_free(theme);
		g_object_unref(settings);
	}
	if (!sName.empty())
		return sName;

	keyFile = g_key_file_new();
	if (g_key_file_load_from_file(keyFile,
			(config + "/gtk-3.0/settings.ini").c_str(), G_KEY_FILE_NONE,
			NULL)) {
		gchar* theme = g_key_file_get_string(keyFile, "Settings",
			"gtk-icon-theme-name", NULL);
		if (theme != NULL) {
			sName = theme;
			g_free(theme);
		}
	}
	g_key_file_free(keyFile);

	return sName;
}


std::string
lookup_in_theme(Theme* theme, const char* name, int size)
{
	// exact size first
	for (const ThemeDir& dir : theme->dirs) {
		if (dir.scale != 1 || !dir_matches_size(dir, size))
			continue;
		for (const std::string& root : theme->roots) {
			for (const char** ext = kExtensions; *ext != NULL; ext++) {
				std::string path = root + "/" + dir.subdir + "/" + name + *ext;
				if (file_exists(path))
					return path;
			}
		}
	}

	// then the closest size
	std::string best;
	int bestDistance = INT_MAX;
	for (const ThemeDir& dir : theme->dirs) {
		int distance = dir_size_distance(dir, size) * dir.scale;
		if (distance >= bestDistance)
			continue;
		for (const std::string& root : theme->roots) {
			for (const char** ext = kExtensions; *ext != NULL; ext++) {
				std::string path = root + "/" + dir.subdir + "/" + name + *ext;
				if (file_exists(path)) {
					best = path;
					bestDistance = distance;
					break;
				}
			}
		}
	}
	return best;
}


std::string
lookup_in_chain(const std::string& themeName, const char* name, int size,
	std::set<std::string>& visited)
{
	if (!visited.insert(themeName).second)
		return "";

	Theme* theme = load_theme(themeName);
	if (theme == NULL)
		return "";

	std::string path = lookup_in_theme(theme, name, size);
	if (!path.empty())
		return path;

	for (const std::string& parent : theme->inherits) {
		path = lookup_in_chain(parent, name, size, visited);
		if (!path.empty())
			return path;
	}
	return "";
}


/*!	Electron, libappindicator and others point IconThemePath at a folder with
	the icon files in it, or at a small theme ("<path>/hicolor/22x22/apps").
	Walk a few levels and take the best size.
*/
std::string
lookup_in_extra_path(const char* folder, const char* name, int size,
	int depth = 0)
{
	std::string best;
	if (depth > 3)
		return best;

	for (const char** ext = kExtensions; *ext != NULL && depth == 0; ext++) {
		std::string path = std::string(folder) + "/" + name + *ext;
		if (file_exists(path))
			return path;
	}

	GDir* dir = g_dir_open(folder, 0, NULL);
	if (dir == NULL)
		return best;

	int bestDistance = INT_MAX;
	const gchar* entry;
	while ((entry = g_dir_read_name(dir)) != NULL) {
		std::string path = std::string(folder) + "/" + entry;
		if (!g_file_test(path.c_str(), G_FILE_TEST_IS_DIR))
			continue;

		for (const char** ext = kExtensions; *ext != NULL; ext++) {
			std::string file = path + "/" + name + *ext;
			if (!file_exists(file))
				continue;

			// "22x22", "scalable", "symbolic"...
			int distance = 1000;
			int dirSize = atoi(entry);
			if (dirSize > 0)
				distance = abs(dirSize - size);
			else if (strstr(file.c_str(), "scalable") != NULL)
				distance = 1;
			if (distance < bestDistance) {
				bestDistance = distance;
				best = file;
			}
		}

		std::string deeper = lookup_in_extra_path(path.c_str(), name, size,
			depth + 1);
		if (!deeper.empty() && best.empty())
			best = deeper;
	}
	g_dir_close(dir);
	return best;
}


cairo_surface_t*
surface_from_argb_network(const guchar* data, int width, int height)
{
	cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
		width, height);
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(surface);
		return NULL;
	}

	cairo_surface_flush(surface);
	unsigned char* bits = cairo_image_surface_get_data(surface);
	int stride = cairo_image_surface_get_stride(surface);
	for (int y = 0; y < height; y++) {
		uint32_t* row = (uint32_t*)(bits + y * stride);
		for (int x = 0; x < width; x++) {
			const guchar* pixel = data + (y * width + x) * 4;
			uint32_t a = pixel[0];
			uint32_t r = pixel[1] * a / 255;
			uint32_t g = pixel[2] * a / 255;
			uint32_t b = pixel[3] * a / 255;
			row[x] = (a << 24) | (r << 16) | (g << 8) | b;
		}
	}
	cairo_surface_mark_dirty(surface);
	return surface;
}


struct PNGReadState {
	const unsigned char*	data;
	size_t					length;
	size_t					offset;
};


cairo_status_t
read_png_data(void* closure, unsigned char* buffer, unsigned int length)
{
	PNGReadState* state = (PNGReadState*)closure;
	if (state->offset + length > state->length)
		return CAIRO_STATUS_READ_ERROR;
	memcpy(buffer, state->data + state->offset, length);
	state->offset += length;
	return CAIRO_STATUS_SUCCESS;
}


}	// namespace


namespace IconLoader {


std::string
FindIcon(const char* name, int size, const char* extraPath)
{
	if (name == NULL || name[0] == '\0')
		return "";

	if (name[0] == '/')
		return file_exists(name) ? name : "";

	if (extraPath != NULL && extraPath[0] != '\0') {
		std::string path = lookup_in_extra_path(extraPath, name, size);
		if (!path.empty())
			return path;
	}

	std::vector<std::string> chain;
	std::string user = user_theme_name();
	if (!user.empty())
		chain.push_back(user);
	chain.push_back("hicolor");
	chain.push_back("breeze");
	chain.push_back("Adwaita");

	// try "a-b-c", then "a-b", then "a", as GTK does
	std::string current = name;
	while (!current.empty()) {
		std::set<std::string> visited;
		for (const std::string& theme : chain) {
			std::string path = lookup_in_chain(theme, current.c_str(), size,
				visited);
			if (!path.empty())
				return path;
		}
		for (const char** ext = kExtensions; *ext != NULL; ext++) {
			std::string path = "/usr/share/pixmaps/" + current + *ext;
			if (file_exists(path))
				return path;
		}

		size_t dash = current.rfind('-');
		if (dash == std::string::npos)
			break;
		current.erase(dash);
	}
	return "";
}


cairo_surface_t*
LoadFile(const char* path, int size)
{
	if (path == NULL || path[0] == '\0')
		return NULL;

	if (g_str_has_suffix(path, ".svg") || g_str_has_suffix(path, ".svgz")) {
#ifdef HAVE_LIBRSVG
		RsvgHandle* handle = rsvg_handle_new_from_file(path, NULL);
		if (handle == NULL)
			return NULL;
		std::string css = color_scheme_stylesheet();
		rsvg_handle_set_stylesheet(handle, (const guint8*)css.c_str(),
			css.size(), NULL);

		cairo_surface_t* surface = cairo_image_surface_create(
			CAIRO_FORMAT_ARGB32, size, size);
		cairo_t* cairo = cairo_create(surface);
		RsvgRectangle viewport = { 0, 0, (double)size, (double)size };
		gboolean rendered = rsvg_handle_render_document(handle, cairo,
			&viewport, NULL);
		cairo_destroy(cairo);
		g_object_unref(handle);
		if (!rendered) {
			cairo_surface_destroy(surface);
			return NULL;
		}
		return surface;
#else
		return NULL;
#endif
	}

	cairo_surface_t* surface = cairo_image_surface_create_from_png(path);
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(surface);
		return NULL;
	}
	return surface;
}


cairo_surface_t*
LoadNamed(const char* name, int size, const char* extraPath)
{
	std::string path = FindIcon(name, size, extraPath);
	if (path.empty())
		return NULL;
	return LoadFile(path.c_str(), size);
}


cairo_surface_t*
LoadPixmaps(GVariant* pixmaps, int size)
{
	if (pixmaps == NULL
		|| !g_variant_is_of_type(pixmaps, G_VARIANT_TYPE("a(iiay)"))) {
		return NULL;
	}

	GVariant* best = NULL;
	int bestWidth = 0;
	gsize count = g_variant_n_children(pixmaps);
	for (gsize i = 0; i < count; i++) {
		GVariant* child = g_variant_get_child_value(pixmaps, i);
		gint32 width, height;
		g_variant_get_child(child, 0, "i", &width);
		g_variant_get_child(child, 1, "i", &height);

		bool better;
		if (best == NULL)
			better = true;
		else if (bestWidth < size)
			better = width > bestWidth;
		else
			better = width >= size && width < bestWidth;

		if (width > 0 && height > 0 && better) {
			if (best != NULL)
				g_variant_unref(best);
			best = child;
			bestWidth = width;
		} else
			g_variant_unref(child);
	}
	if (best == NULL)
		return NULL;

	gint32 width, height;
	g_variant_get_child(best, 0, "i", &width);
	g_variant_get_child(best, 1, "i", &height);
	GVariant* bytes = g_variant_get_child_value(best, 2);
	gsize length;
	const guchar* data = (const guchar*)g_variant_get_fixed_array(bytes,
		&length, 1);

	cairo_surface_t* surface = NULL;
	if (length >= (gsize)width * height * 4)
		surface = surface_from_argb_network(data, width, height);

	g_variant_unref(bytes);
	g_variant_unref(best);
	return surface;
}


cairo_surface_t*
LoadPNGData(const void* data, size_t length)
{
	if (data == NULL || length == 0)
		return NULL;

	PNGReadState state = { (const unsigned char*)data, length, 0 };
	cairo_surface_t* surface = cairo_image_surface_create_from_png_stream(
		&read_png_data, &state);
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(surface);
		return NULL;
	}
	return surface;
}


BBitmap*
MakeBitmap(cairo_surface_t* icon, int size, cairo_surface_t* overlay)
{
	if (icon == NULL || size <= 0)
		return NULL;

	cairo_surface_t* target = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
		size, size);
	cairo_t* cairo = cairo_create(target);

	int width = cairo_image_surface_get_width(icon);
	int height = cairo_image_surface_get_height(icon);
	if (width > 0 && height > 0) {
		double scale = std::min((double)size / width, (double)size / height);
		cairo_save(cairo);
		cairo_translate(cairo, (size - width * scale) / 2,
			(size - height * scale) / 2);
		cairo_scale(cairo, scale, scale);
		cairo_set_source_surface(cairo, icon, 0, 0);
		cairo_pattern_set_filter(cairo_get_source(cairo), CAIRO_FILTER_GOOD);
		cairo_paint(cairo);
		cairo_restore(cairo);
	}

	if (overlay != NULL) {
		int overlayWidth = cairo_image_surface_get_width(overlay);
		int overlayHeight = cairo_image_surface_get_height(overlay);
		if (overlayWidth > 0 && overlayHeight > 0) {
			double half = size / 2.0;
			double scale = std::min(half / overlayWidth,
				half / overlayHeight);
			cairo_save(cairo);
			cairo_translate(cairo, size - overlayWidth * scale,
				size - overlayHeight * scale);
			cairo_scale(cairo, scale, scale);
			cairo_set_source_surface(cairo, overlay, 0, 0);
			cairo_paint(cairo);
			cairo_restore(cairo);
		}
	}
	cairo_destroy(cairo);
	cairo_surface_flush(target);

	BBitmap* bitmap = new BBitmap(BRect(0, 0, size - 1, size - 1), B_RGBA32);
	if (bitmap->InitCheck() != B_OK) {
		delete bitmap;
		cairo_surface_destroy(target);
		return NULL;
	}

	// cairo is premultiplied native-endian ARGB, B_RGBA32 is straight BGRA
	const unsigned char* source = cairo_image_surface_get_data(target);
	int sourceStride = cairo_image_surface_get_stride(target);
	uint8* dest = (uint8*)bitmap->Bits();
	int32 destStride = bitmap->BytesPerRow();
	for (int y = 0; y < size; y++) {
		const uint32_t* in = (const uint32_t*)(source + y * sourceStride);
		uint8* out = dest + y * destStride;
		for (int x = 0; x < size; x++) {
			uint32_t pixel = in[x];
			uint32_t a = pixel >> 24;
			uint32_t r = (pixel >> 16) & 0xff;
			uint32_t g = (pixel >> 8) & 0xff;
			uint32_t b = pixel & 0xff;
			if (a != 0 && a != 255) {
				r = std::min<uint32_t>(255, r * 255 / a);
				g = std::min<uint32_t>(255, g * 255 / a);
				b = std::min<uint32_t>(255, b * 255 / a);
			}
			out[x * 4 + 0] = b;
			out[x * 4 + 1] = g;
			out[x * 4 + 2] = r;
			out[x * 4 + 3] = a;
		}
	}
	cairo_surface_destroy(target);
	return bitmap;
}


}	// namespace IconLoader
