/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 *
 * com.canonical.dbusmenu client: turns an app's tray menu into a BMessage
 * tree that the replicant builds a BPopUpMenu from.
 */


#include "DBusMenu.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>

#include <vector>

#include <Bitmap.h>
#include <Message.h>

#include "IconLoader.h"
#include "TrayBridgeProtocol.h"


static const char* kInterface = "com.canonical.dbusmenu";
static const int kTimeout = 1500;
static const int kMenuIconSize = 16;


namespace {


struct MenuRequest {
	GDBusConnection*	connection;
	std::string			service;
	std::string			path;
	std::string			iconThemePath;
	int					x;
	int					y;
	BMessenger			view;
};


/*!	dbusmenu labels mark the access key with "_" and write a literal
	underscore as "__". Haiku menus have no access keys.
*/
std::string
strip_mnemonic(const char* label)
{
	std::string result;
	for (const char* c = label; *c != '\0'; c++) {
		if (*c == '_') {
			if (c[1] == '_') {
				result += '_';
				c++;
			}
			continue;
		}
		result += *c;
	}
	return result;
}


void
add_icon(BMessage& node, GVariant* properties, const std::string& themePath)
{
	cairo_surface_t* surface = NULL;

	GVariant* data = g_variant_lookup_value(properties, "icon-data",
		G_VARIANT_TYPE_BYTESTRING);
	if (data != NULL) {
		gsize length;
		const void* bytes = g_variant_get_fixed_array(data, &length, 1);
		surface = IconLoader::LoadPNGData(bytes, length);
		g_variant_unref(data);
	}

	const char* name = NULL;
	if (surface == NULL
		&& g_variant_lookup(properties, "icon-name", "&s", &name)
		&& name[0] != '\0') {
		surface = IconLoader::LoadNamed(name, kMenuIconSize,
			themePath.c_str());
	}

	if (surface == NULL)
		return;

	BBitmap* bitmap = IconLoader::MakeBitmap(surface, kMenuIconSize);
	cairo_surface_destroy(surface);
	if (bitmap == NULL)
		return;

	BMessage archive;
	if (bitmap->Archive(&archive) == B_OK)
		node.AddMessage("icon", &archive);
	delete bitmap;
}


/*!	Converts one (ia{sv}av) layout node into a BMessage; returns false for
	nodes that are not shown.
*/
bool
convert_node(GVariant* layout, BMessage& node, const std::string& themePath,
	std::vector<int32>* submenuIDs)
{
	gint32 id;
	GVariant* properties;
	GVariant* children;
	g_variant_get(layout, "(i@a{sv}@av)", &id, &properties, &children);

	gboolean visible = TRUE;
	g_variant_lookup(properties, "visible", "b", &visible);
	if (!visible) {
		g_variant_unref(properties);
		g_variant_unref(children);
		return false;
	}

	node.AddInt32("id", id);

	const char* type = NULL;
	if (g_variant_lookup(properties, "type", "&s", &type)
		&& strcmp(type, "separator") == 0) {
		node.AddBool("separator", true);
	} else {
		const char* label = "";
		g_variant_lookup(properties, "label", "&s", &label);
		node.AddString("label", strip_mnemonic(label).c_str());

		gboolean enabled = TRUE;
		g_variant_lookup(properties, "enabled", "b", &enabled);
		node.AddBool("enabled", enabled);

		const char* toggleType = NULL;
		if (g_variant_lookup(properties, "toggle-type", "&s", &toggleType)) {
			if (strcmp(toggleType, "checkmark") == 0)
				node.AddInt32("toggle", 1);
			else if (strcmp(toggleType, "radio") == 0)
				node.AddInt32("toggle", 2);
		}
		gint32 toggleState = 0;
		if (g_variant_lookup(properties, "toggle-state", "i", &toggleState))
			node.AddBool("checked", toggleState == 1);

		add_icon(node, properties, themePath);
	}

	const char* childrenDisplay = NULL;
	bool isSubmenu = g_variant_lookup(properties, "children-display", "&s",
			&childrenDisplay)
		&& strcmp(childrenDisplay, "submenu") == 0;

	gsize count = g_variant_n_children(children);
	if (count > 0 || isSubmenu) {
		if (submenuIDs != NULL && id != 0)
			submenuIDs->push_back(id);

		for (gsize i = 0; i < count; i++) {
			GVariant* boxed = g_variant_get_child_value(children, i);
			GVariant* child = g_variant_get_variant(boxed);
			BMessage childNode;
			if (convert_node(child, childNode, themePath, submenuIDs))
				node.AddMessage("child", &childNode);
			g_variant_unref(child);
			g_variant_unref(boxed);
		}
	}

	g_variant_unref(properties);
	g_variant_unref(children);
	return true;
}


GVariant*
get_layout(MenuRequest* request)
{
	GError* error = NULL;
	GVariant* result = g_dbus_connection_call_sync(request->connection,
		request->service.c_str(), request->path.c_str(), kInterface,
		"GetLayout", g_variant_new("(ii@as)", 0, -1,
			g_variant_new_strv(NULL, 0)),
		G_VARIANT_TYPE("(u(ia{sv}av))"), G_DBUS_CALL_FLAGS_NONE, kTimeout,
		NULL, &error);
	if (result == NULL) {
		fprintf(stderr, "TrayBridge: GetLayout on %s%s failed: %s\n",
			request->service.c_str(), request->path.c_str(), error->message);
		g_error_free(error);
		return NULL;
	}

	GVariant* layout = g_variant_get_child_value(result, 1);
	g_variant_unref(result);
	return layout;
}


void
about_to_show(MenuRequest* request, int32 id)
{
	GVariant* result = g_dbus_connection_call_sync(request->connection,
		request->service.c_str(), request->path.c_str(), kInterface,
		"AboutToShow", g_variant_new("(i)", id), NULL,
		G_DBUS_CALL_FLAGS_NONE, kTimeout, NULL, NULL);
	if (result != NULL)
		g_variant_unref(result);
}


/*!	Asks the app to fill the given submenus; returns true when the layout
	must be fetched again.
*/
bool
about_to_show_group(MenuRequest* request, const std::vector<int32>& ids)
{
	if (ids.empty())
		return false;

	GVariantBuilder builder;
	g_variant_builder_init(&builder, G_VARIANT_TYPE("ai"));
	for (int32 id : ids)
		g_variant_builder_add(&builder, "i", id);

	GVariant* result = g_dbus_connection_call_sync(request->connection,
		request->service.c_str(), request->path.c_str(), kInterface,
		"AboutToShowGroup", g_variant_new("(ai)", &builder),
		G_VARIANT_TYPE("(aiai)"), G_DBUS_CALL_FLAGS_NONE, kTimeout, NULL,
		NULL);
	if (result == NULL) {
		// older implementations: one at a time
		size_t limit = std::min<size_t>(ids.size(), 32);
		for (size_t i = 0; i < limit; i++)
			about_to_show(request, ids[i]);
		return true;
	}

	GVariant* updates = g_variant_get_child_value(result, 0);
	bool needsUpdate = g_variant_n_children(updates) > 0;
	g_variant_unref(updates);
	g_variant_unref(result);
	return needsUpdate;
}


gpointer
fetch_menu(gpointer data)
{
	MenuRequest* request = (MenuRequest*)data;

	about_to_show(request, 0);

	GVariant* layout = get_layout(request);
	if (layout != NULL) {
		BMessage root;
		std::vector<int32> submenus;
		convert_node(layout, root, request->iconThemePath, &submenus);

		if (about_to_show_group(request, submenus)) {
			g_variant_unref(layout);
			layout = get_layout(request);
			root.MakeEmpty();
			if (layout != NULL)
				convert_node(layout, root, request->iconThemePath, NULL);
		}

		if (layout != NULL) {
			BMessage show(kTBShowMenu);
			show.AddMessage("menu", &root);
			show.AddPoint("where", BPoint(request->x, request->y));
			request->view.SendMessage(&show);
			g_variant_unref(layout);

			// Qt and Electron track whether their menu is open
			GVariant* result = g_dbus_connection_call_sync(
				request->connection, request->service.c_str(),
				request->path.c_str(), kInterface, "Event",
				g_variant_new("(isvu)", 0, "opened",
					g_variant_new_int32(0), 0),
				NULL, G_DBUS_CALL_FLAGS_NONE, kTimeout, NULL, NULL);
			if (result != NULL)
				g_variant_unref(result);
		}
	}

	g_object_unref(request->connection);
	delete request;
	return NULL;
}


}	// namespace


namespace DBusMenu {


void
ShowAsync(GDBusConnection* connection, const std::string& service,
	const std::string& path, const std::string& iconThemePath, int x, int y,
	const BMessenger& view)
{
	MenuRequest* request = new MenuRequest;
	request->connection = (GDBusConnection*)g_object_ref(connection);
	request->service = service;
	request->path = path;
	request->iconThemePath = iconThemePath;
	request->x = x;
	request->y = y;
	request->view = view;

	GThread* thread = g_thread_new("traybridge menu", &fetch_menu, request);
	g_thread_unref(thread);
}


void
SendClicked(GDBusConnection* connection, const std::string& service,
	const std::string& path, int32 id)
{
	g_dbus_connection_call(connection, service.c_str(), path.c_str(),
		kInterface, "Event",
		g_variant_new("(isvu)", id, "clicked", g_variant_new_int32(0),
			(guint32)(g_get_real_time() / 1000)),
		NULL, G_DBUS_CALL_FLAGS_NONE, kTimeout, NULL, NULL, NULL);
	SendClosed(connection, service, path);
}


void
SendClosed(GDBusConnection* connection, const std::string& service,
	const std::string& path)
{
	g_dbus_connection_call(connection, service.c_str(), path.c_str(),
		kInterface, "Event",
		g_variant_new("(isvu)", 0, "closed", g_variant_new_int32(0), 0),
		NULL, G_DBUS_CALL_FLAGS_NONE, kTimeout, NULL, NULL, NULL);
}


}	// namespace DBusMenu
