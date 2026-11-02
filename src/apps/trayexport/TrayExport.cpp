/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 *
 * TrayExport: apps' Deskbar items in the Linux desktop's own tray, for when
 * Hokku's Deskbar isn't running (a Plasma, GNOME or sway session).
 *
 * BDeskbar talks to it as it would to Deskbar. Each item is a replicant run
 * by its own app (a remote replicant, see RemoteReplicant.h); what it draws
 * is shown as StatusNotifierItems. Trays show square icons, so a wide item
 * is cut into square tiles, one icon each (PowerPlay's VU meter: one per
 * needle). A click on a tile reaches the replicant where that tile is, so
 * the replicant answers as it does in Deskbar.
 *
 * Each tile has a D-Bus connection of its own, so the tray notices when it
 * goes. All D-Bus work runs on a GLib main loop thread; the BApplication
 * thread owns the items and hands D-Bus work over with g_main_context_invoke.
 */


#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <condition_variable>
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <gio/gio.h>

#include <Application.h>
#include <View.h>
#include <MessageRunner.h>
#include <Path.h>
#include <Roster.h>
#include <String.h>

#include <DeskbarPrivate.h>
#include <RemoteReplicant.h>


using namespace BPrivate;

#define TRAY_EXPORT_SIGNATURE "application/x-vnd.Hokku-TrayExport"
#define TRAY_BRIDGE_SIGNATURE "application/x-vnd.Hokku-TrayBridge"

static const uint32 kMsgCheck = 'teCk';
static const uint32 kMsgTrayEvent = 'teEv';
static const uint32 kMsgMenuRequest = 'teMq';
static const uint32 kMsgMenuChosen = 'teMc';
// How long a tray waits for the replicant's menu before it opens nothing.
static const guint kMenuTimeoutMs = 500;
static const bigtime_t kCheckInterval = 1000000;
// Trays fetch the whole icon on every NewIcon: no more than this often.
static const bigtime_t kMinIconInterval = 66000;

static const char* kItemInterface = "org.kde.StatusNotifierItem";
static const char* kWatcherName = "org.kde.StatusNotifierWatcher";
static const char* kItemPath = "/StatusNotifierItem";
static const char* kMenuPath = "/MenuBar";
static const char* kMenuInterface = "com.canonical.dbusmenu";

enum {
	kEventActivate,
	kEventSecondaryActivate,
	kEventContextMenu,
	kEventScroll
};


static bool
debugging()
{
	static int sDebug = -1;
	if (sDebug < 0)
		sDebug = getenv("COSMOE_DEBUG_REMOTE_REPLICANTS") != NULL ? 1 : 0;
	return sDebug == 1;
}

#define TRACE(...) do { \
	if (debugging()) { \
		fprintf(stderr, "TrayExport: "); \
		fprintf(stderr, __VA_ARGS__); \
	} \
} while (0)


static const char* kIntrospection =
	"<node>"
	" <interface name='org.kde.StatusNotifierItem'>"
	"  <property name='Category' type='s' access='read'/>"
	"  <property name='Id' type='s' access='read'/>"
	"  <property name='Title' type='s' access='read'/>"
	"  <property name='Status' type='s' access='read'/>"
	"  <property name='WindowId' type='i' access='read'/>"
	"  <property name='IconName' type='s' access='read'/>"
	"  <property name='IconPixmap' type='a(iiay)' access='read'/>"
	"  <property name='OverlayIconName' type='s' access='read'/>"
	"  <property name='OverlayIconPixmap' type='a(iiay)' access='read'/>"
	"  <property name='AttentionIconName' type='s' access='read'/>"
	"  <property name='AttentionIconPixmap' type='a(iiay)' access='read'/>"
	"  <property name='ToolTip' type='(sa(iiay)ss)' access='read'/>"
	"  <property name='ItemIsMenu' type='b' access='read'/>"
	"  <property name='Menu' type='o' access='read'/>"
	"  <method name='Activate'><arg type='i' direction='in'/>"
	"   <arg type='i' direction='in'/></method>"
	"  <method name='SecondaryActivate'><arg type='i' direction='in'/>"
	"   <arg type='i' direction='in'/></method>"
	"  <method name='ContextMenu'><arg type='i' direction='in'/>"
	"   <arg type='i' direction='in'/></method>"
	"  <method name='Scroll'><arg type='i' direction='in'/>"
	"   <arg type='s' direction='in'/></method>"
	"  <signal name='NewIcon'/>"
	"  <signal name='NewTitle'/>"
	"  <signal name='NewToolTip'/>"
	"  <signal name='NewStatus'><arg type='s'/></signal>"
	" </interface>"
	" <interface name='com.canonical.dbusmenu'>"
	"  <property name='Version' type='u' access='read'/>"
	"  <property name='TextDirection' type='s' access='read'/>"
	"  <property name='Status' type='s' access='read'/>"
	"  <property name='IconThemePath' type='as' access='read'/>"
	"  <method name='GetLayout'><arg type='i' direction='in'/>"
	"   <arg type='i' direction='in'/><arg type='as' direction='in'/>"
	"   <arg type='u' direction='out'/><arg type='(ia{sv}av)' direction='out'/>"
	"  </method>"
	"  <method name='GetGroupProperties'><arg type='ai' direction='in'/>"
	"   <arg type='as' direction='in'/><arg type='a(ia{sv})' direction='out'/>"
	"  </method>"
	"  <method name='GetProperty'><arg type='i' direction='in'/>"
	"   <arg type='s' direction='in'/><arg type='v' direction='out'/></method>"
	"  <method name='Event'><arg type='i' direction='in'/>"
	"   <arg type='s' direction='in'/><arg type='v' direction='in'/>"
	"   <arg type='u' direction='in'/></method>"
	"  <method name='EventGroup'><arg type='a(isvu)' direction='in'/>"
	"   <arg type='ai' direction='out'/></method>"
	"  <method name='AboutToShow'><arg type='i' direction='in'/>"
	"   <arg type='b' direction='out'/></method>"
	"  <method name='AboutToShowGroup'><arg type='ai' direction='in'/>"
	"   <arg type='ai' direction='out'/><arg type='ai' direction='out'/>"
	"  </method>"
	"  <signal name='LayoutUpdated'><arg type='u'/><arg type='i'/></signal>"
	"  <signal name='ItemsPropertiesUpdated'><arg type='a(ia{sv})'/>"
	"   <arg type='a(ias)'/></signal>"
	" </interface>"
	"</node>";


static GDBusNodeInfo* sNodeInfo = NULL;
static GMainContext* sContext = NULL;
static GMainLoop* sLoop = NULL;
static std::string sBusAddress;


// Runs f on the GLib thread.
static void
on_glib(std::function<void()> f)
{
	auto* call = new std::function<void()>(std::move(f));
	g_main_context_invoke_full(sContext, G_PRIORITY_DEFAULT,
		[](gpointer data) -> gboolean {
			auto* call = (std::function<void()>*)data;
			(*call)();
			return G_SOURCE_REMOVE;
		}, call, [](gpointer data) {
			delete (std::function<void()>*)data;
		});
}


class Item;


/*!	The replicant's last menu, as a tree of dbusmenu nodes (id 0 is the
	root). Built on the BApplication thread, read on the GLib thread.
*/
struct MenuNode {
	std::string			label;
	bool				enabled = true;
	bool				marked = false;
	bool				separator = false;
	std::vector<int32>	children;
	std::vector<int32>	path;		// item indices, for the replicant
};

struct MenuModel {
	std::mutex			lock;
	uint32				revision = 1;
	std::vector<MenuNode> nodes = std::vector<MenuNode>(1);
	// AboutToShow(0) calls waiting for the replicant's menu
	std::vector<GDBusMethodInvocation*> waiting;
	std::vector<GDBusConnection*> connections;	// to announce new layouts
};


/*!	One icon: a square piece of an item. Lives on the GLib thread once
	exported; its pixmaps are written by the BApplication thread under
	fLock.
*/
struct Tile {
	Item*				item = NULL;
	int32				itemID = 0;
	int32				index = 0;
	int32				count = 1;
	std::string			id;
	std::string			title;

	std::mutex			lock;
	std::vector<uint8>	pixmap;		// ARGB32 in network byte order
	std::vector<uint8>	pixmap2x;
	int32				size = 0;

	GDBusConnection*	connection = NULL;
	guint				registration = 0;
	std::string			busName;
	bigtime_t			lastSignal = 0;
	bool				signalPending = false;
	GSource*			pendingSignal = NULL;
	bool				announced = false;
	std::shared_ptr<MenuModel> menu;
	guint				menuRegistration = 0;
};


static GVariant*
pixmap_variant(Tile* tile)
{
	GVariantBuilder builder;
	g_variant_builder_init(&builder, G_VARIANT_TYPE("a(iiay)"));
	std::lock_guard<std::mutex> _(tile->lock);
	if (tile->size > 0) {
		const std::vector<uint8>* pixmaps[] = {&tile->pixmap, &tile->pixmap2x};
		for (int i = 0; i < 2; i++) {
			int32 size = tile->size * (i + 1);
			GVariant* bytes = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE,
				pixmaps[i]->data(), pixmaps[i]->size(), 1);
			g_variant_builder_add(&builder, "(ii@ay)", size, size, bytes);
		}
	}
	return g_variant_builder_end(&builder);
}


static GVariant*
get_property(GDBusConnection*, const gchar*, const gchar*, const gchar*,
	const gchar* name, GError**, gpointer data)
{
	Tile* tile = (Tile*)data;
	if (strcmp(name, "Category") == 0)
		return g_variant_new_string("ApplicationStatus");
	if (strcmp(name, "Id") == 0)
		return g_variant_new_string(tile->id.c_str());
	if (strcmp(name, "Title") == 0)
		return g_variant_new_string(tile->title.c_str());
	if (strcmp(name, "Status") == 0)
		return g_variant_new_string("Active");
	if (strcmp(name, "WindowId") == 0)
		return g_variant_new_int32(0);
	if (strcmp(name, "IconPixmap") == 0)
		return pixmap_variant(tile);
	if (strcmp(name, "OverlayIconPixmap") == 0
		|| strcmp(name, "AttentionIconPixmap") == 0)
		return g_variant_new_array(G_VARIANT_TYPE("(iiay)"), NULL, 0);
	if (strcmp(name, "ToolTip") == 0) {
		return g_variant_new("(s@a(iiay)ss)", "",
			g_variant_new_array(G_VARIANT_TYPE("(iiay)"), NULL, 0),
			tile->title.c_str(), "");
	}
	if (strcmp(name, "ItemIsMenu") == 0)
		return g_variant_new_boolean(FALSE);
	if (strcmp(name, "Menu") == 0)
		return g_variant_new_object_path(kMenuPath);
	// IconName, OverlayIconName, AttentionIconName
	return g_variant_new_string("");
}


static void
method_call(GDBusConnection*, const gchar*, const gchar*, const gchar*,
	const gchar* method, GVariant* parameters,
	GDBusMethodInvocation* invocation, gpointer data)
{
	Tile* tile = (Tile*)data;
	BMessage event(kMsgTrayEvent);
	event.AddInt32("item", tile->itemID);
	event.AddInt32("tile", tile->index);
	if (strcmp(method, "Scroll") == 0) {
		gint32 delta;
		const gchar* orientation;
		g_variant_get(parameters, "(i&s)", &delta, &orientation);
		event.AddInt32("kind", kEventScroll);
		event.AddInt32("delta", delta);
		event.AddBool("horizontal", strcasecmp(orientation, "horizontal") == 0);
	} else {
		gint32 x, y;
		g_variant_get(parameters, "(ii)", &x, &y);
		event.AddInt32("kind", strcmp(method, "Activate") == 0 ? kEventActivate
			: strcmp(method, "SecondaryActivate") == 0
				? kEventSecondaryActivate : kEventContextMenu);
		event.AddPoint("screen", BPoint(x, y));
	}
	be_app->PostMessage(&event);
	g_dbus_method_invocation_return_value(invocation, NULL);
}


static const GDBusInterfaceVTable kVTable = {
	method_call, get_property, NULL, {0}
};


//	#pragma mark - dbusmenu


static std::string
escape_mnemonic(const std::string& label)
{
	// dbusmenu treats "_" as a mnemonic marker
	std::string escaped;
	for (char c : label) {
		if (c == '_')
			escaped += '_';
		escaped += c;
	}
	return escaped;
}


// Lock held.
static GVariant*
node_properties(MenuModel* model, int32 id)
{
	GVariantBuilder properties;
	g_variant_builder_init(&properties, G_VARIANT_TYPE("a{sv}"));
	if (id < 0 || id >= (int32)model->nodes.size())
		return g_variant_builder_end(&properties);
	const MenuNode& node = model->nodes[id];
	if (id == 0 || !node.children.empty()) {
		g_variant_builder_add(&properties, "{sv}", "children-display",
			g_variant_new_string("submenu"));
	}
	if (id != 0) {
		if (node.separator) {
			g_variant_builder_add(&properties, "{sv}", "type",
				g_variant_new_string("separator"));
		} else {
			g_variant_builder_add(&properties, "{sv}", "label",
				g_variant_new_string(escape_mnemonic(node.label).c_str()));
			g_variant_builder_add(&properties, "{sv}", "enabled",
				g_variant_new_boolean(node.enabled));
			if (node.marked) {
				g_variant_builder_add(&properties, "{sv}", "toggle-type",
					g_variant_new_string("checkmark"));
				g_variant_builder_add(&properties, "{sv}", "toggle-state",
					g_variant_new_int32(1));
			}
		}
		g_variant_builder_add(&properties, "{sv}", "visible",
			g_variant_new_boolean(TRUE));
	}
	return g_variant_builder_end(&properties);
}


// Lock held.
static GVariant*
node_layout(MenuModel* model, int32 id, int32 depth)
{
	GVariantBuilder children;
	g_variant_builder_init(&children, G_VARIANT_TYPE("av"));
	if (depth != 0 && id >= 0 && id < (int32)model->nodes.size()) {
		for (int32 child : model->nodes[id].children) {
			g_variant_builder_add(&children, "v",
				node_layout(model, child, depth < 0 ? -1 : depth - 1));
		}
	}
	return g_variant_new("(i@a{sv}@av)", id, node_properties(model, id),
		g_variant_builder_end(&children));
}


// On the GLib thread: answer the trays waiting to show the menu.
static void
answer_waiting(MenuModel* model, bool updated)
{
	std::vector<GDBusMethodInvocation*> waiting;
	uint32 revision;
	std::vector<GDBusConnection*> connections;
	{
		std::lock_guard<std::mutex> _(model->lock);
		waiting.swap(model->waiting);
		revision = model->revision;
		connections = model->connections;
	}
	for (GDBusMethodInvocation* invocation : waiting) {
		g_dbus_method_invocation_return_value(invocation,
			g_variant_new("(b)", updated));
	}
	if (updated) {
		for (GDBusConnection* connection : connections) {
			g_dbus_connection_emit_signal(connection, NULL, kMenuPath,
				kMenuInterface, "LayoutUpdated",
				g_variant_new("(ui)", revision, 0), NULL);
		}
	}
}


static void
menu_method_call(GDBusConnection*, const gchar*, const gchar*, const gchar*,
	const gchar* method, GVariant* parameters,
	GDBusMethodInvocation* invocation, gpointer data)
{
	Tile* tile = (Tile*)data;
	MenuModel* model = tile->menu.get();

	if (strcmp(method, "GetLayout") == 0) {
		gint32 parent, depth;
		g_variant_get(parameters, "(ii@as)", &parent, &depth, NULL);
		std::lock_guard<std::mutex> _(model->lock);
		g_dbus_method_invocation_return_value(invocation,
			g_variant_new("(u@(ia{sv}av))", model->revision,
				node_layout(model, parent, depth)));
	} else if (strcmp(method, "GetGroupProperties") == 0) {
		GVariantIter* ids;
		g_variant_get(parameters, "(ai@as)", &ids, NULL);
		GVariantBuilder result;
		g_variant_builder_init(&result, G_VARIANT_TYPE("a(ia{sv})"));
		gint32 id;
		{
			std::lock_guard<std::mutex> _(model->lock);
			while (g_variant_iter_next(ids, "i", &id)) {
				g_variant_builder_add(&result, "(i@a{sv})", id,
					node_properties(model, id));
			}
		}
		g_variant_iter_free(ids);
		g_dbus_method_invocation_return_value(invocation,
			g_variant_new("(@a(ia{sv}))", g_variant_builder_end(&result)));
	} else if (strcmp(method, "GetProperty") == 0) {
		gint32 id;
		const gchar* name;
		g_variant_get(parameters, "(i&s)", &id, &name);
		GVariant* value = NULL;
		{
			std::lock_guard<std::mutex> _(model->lock);
			GVariant* properties = node_properties(model, id);
			value = g_variant_lookup_value(properties, name, NULL);
			g_variant_unref(g_variant_ref_sink(properties));
		}
		if (value == NULL) {
			g_dbus_method_invocation_return_dbus_error(invocation,
				"com.canonical.dbusmenu.Error.UnknownProperty", name);
		} else {
			g_dbus_method_invocation_return_value(invocation,
				g_variant_new("(v)", value));
			g_variant_unref(value);
		}
	} else if (strcmp(method, "Event") == 0 || strcmp(method, "EventGroup") == 0) {
		std::vector<std::pair<gint32, std::string>> events;
		if (strcmp(method, "Event") == 0) {
			gint32 id;
			const gchar* event;
			g_variant_get(parameters, "(i&s@vu)", &id, &event, NULL, NULL);
			events.push_back({id, event});
		} else {
			GVariantIter* iter;
			g_variant_get(parameters, "(a(isvu))", &iter);
			gint32 id;
			const gchar* event;
			while (g_variant_iter_next(iter, "(i&svu)", &id, &event, NULL,
					NULL))
				events.push_back({id, event});
			g_variant_iter_free(iter);
		}
		for (auto& event : events) {
			if (event.second != "clicked")
				continue;
			BMessage chosen(kMsgMenuChosen);
			chosen.AddInt32("item", tile->itemID);
			std::lock_guard<std::mutex> _(model->lock);
			if (event.first <= 0 || event.first >= (int32)model->nodes.size())
				continue;
			for (int32 index : model->nodes[event.first].path)
				chosen.AddInt32("path", index);
			be_app->PostMessage(&chosen);
		}
		if (strcmp(method, "Event") == 0)
			g_dbus_method_invocation_return_value(invocation, NULL);
		else {
			g_dbus_method_invocation_return_value(invocation,
				g_variant_new("(@ai)",
					g_variant_new_array(G_VARIANT_TYPE_INT32, NULL, 0)));
		}
	} else if (strcmp(method, "AboutToShow") == 0) {
		gint32 id;
		g_variant_get(parameters, "(i)", &id);
		if (id != 0) {
			// The whole tree comes at once: submenus are up to date.
			g_dbus_method_invocation_return_value(invocation,
				g_variant_new("(b)", FALSE));
			return;
		}
		// The menu is the replicant's to make: click it as the tray would
		// have, and answer when its menu arrives (or nothing does).
		{
			std::lock_guard<std::mutex> _(model->lock);
			model->waiting.push_back(invocation);
		}
		BMessage request(kMsgMenuRequest);
		request.AddInt32("item", tile->itemID);
		request.AddInt32("tile", tile->index);
		be_app->PostMessage(&request);
		std::shared_ptr<MenuModel>* keep = new std::shared_ptr<MenuModel>(
			tile->menu);
		GSource* timeout = g_timeout_source_new(kMenuTimeoutMs);
		g_source_set_callback(timeout, [](gpointer data) -> gboolean {
			answer_waiting(((std::shared_ptr<MenuModel>*)data)->get(), false);
			return G_SOURCE_REMOVE;
		}, keep, [](gpointer data) {
			delete (std::shared_ptr<MenuModel>*)data;
		});
		g_source_attach(timeout, sContext);
		g_source_unref(timeout);
	} else if (strcmp(method, "AboutToShowGroup") == 0) {
		g_dbus_method_invocation_return_value(invocation,
			g_variant_new("(@ai@ai)",
				g_variant_new_array(G_VARIANT_TYPE_INT32, NULL, 0),
				g_variant_new_array(G_VARIANT_TYPE_INT32, NULL, 0)));
	} else
		g_dbus_method_invocation_return_value(invocation, NULL);
}


static GVariant*
menu_get_property(GDBusConnection*, const gchar*, const gchar*, const gchar*,
	const gchar* name, GError**, gpointer)
{
	if (strcmp(name, "Version") == 0)
		return g_variant_new_uint32(3);
	if (strcmp(name, "TextDirection") == 0)
		return g_variant_new_string("ltr");
	if (strcmp(name, "Status") == 0)
		return g_variant_new_string("normal");
	return g_variant_new_array(G_VARIANT_TYPE_STRING, NULL, 0);
}


static const GDBusInterfaceVTable kMenuVTable = {
	menu_method_call, menu_get_property, NULL, {0}
};


// On the GLib thread: put a tile on the bus and announce it to the tray.
static bool
export_tile(Tile* tile)
{
	GError* error = NULL;
	tile->connection = g_dbus_connection_new_for_address_sync(
		sBusAddress.c_str(),
		(GDBusConnectionFlags)(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT
			| G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
		NULL, NULL, &error);
	if (tile->connection == NULL) {
		fprintf(stderr, "TrayExport: can't reach the session bus: %s\n",
			error->message);
		g_error_free(error);
		return false;
	}
	tile->registration = g_dbus_connection_register_object(tile->connection,
		kItemPath, sNodeInfo->interfaces[0], &kVTable, tile, NULL, &error);
	if (tile->registration == 0) {
		fprintf(stderr, "TrayExport: can't export an item: %s\n",
			error->message);
		g_error_free(error);
		return false;
	}
	tile->menuRegistration = g_dbus_connection_register_object(
		tile->connection, kMenuPath, sNodeInfo->interfaces[1], &kMenuVTable,
		tile, NULL, &error);
	if (tile->menuRegistration == 0) {
		fprintf(stderr, "TrayExport: can't export a menu: %s\n",
			error->message);
		g_clear_error(&error);
	}
	{
		std::lock_guard<std::mutex> _(tile->menu->lock);
		tile->menu->connections.push_back(tile->connection);
	}
	tile->busName = g_dbus_connection_get_unique_name(tile->connection);

	// Is there a tray? Asked of the bus itself: a tray asks the item for
	// its properties as soon as it's registered, so registering has to be
	// asynchronous, or this thread couldn't answer.
	GVariant* owner = g_dbus_connection_call_sync(tile->connection,
		"org.freedesktop.DBus", "/org/freedesktop/DBus",
		"org.freedesktop.DBus", "NameHasOwner",
		g_variant_new("(s)", kWatcherName), G_VARIANT_TYPE("(b)"),
		G_DBUS_CALL_FLAGS_NONE, 2000, NULL, &error);
	gboolean hasTray = FALSE;
	if (owner != NULL) {
		g_variant_get(owner, "(b)", &hasTray);
		g_variant_unref(owner);
	} else
		g_clear_error(&error);
	if (!hasTray) {
		fprintf(stderr, "TrayExport: the desktop has no tray "
			"(no StatusNotifierWatcher)\n");
		return false;
	}
	TRACE("tile %s %" B_PRId32 "/%" B_PRId32 " on the bus as %s\n",
		tile->id.c_str(), tile->index + 1, tile->count, tile->busName.c_str());
	return true;
}


// On the GLib thread: tell the tray about a tile, once it has a picture
// (so the tray never shows it empty).
static void
announce_tile(Tile* tile)
{
	if (tile->announced || tile->connection == NULL)
		return;
	tile->announced = true;
	g_dbus_connection_call(tile->connection, kWatcherName,
		"/StatusNotifierWatcher", kWatcherName, "RegisterStatusNotifierItem",
		g_variant_new("(s)", tile->busName.c_str()), NULL,
		G_DBUS_CALL_FLAGS_NONE, -1, NULL,
		[](GObject* source, GAsyncResult* result, gpointer) {
			GError* error = NULL;
			GVariant* reply = g_dbus_connection_call_finish(
				G_DBUS_CONNECTION(source), result, &error);
			if (reply != NULL)
				g_variant_unref(reply);
			else {
				fprintf(stderr, "TrayExport: the tray didn't take an item: "
					"%s\n", error->message);
				g_error_free(error);
			}
		}, NULL);
}


// On the GLib thread: take a tile off the bus. Closing its connection is
// what tells the tray it's gone.
static void
unexport_tile(Tile* tile)
{
	if (tile->pendingSignal != NULL)
		g_source_destroy(tile->pendingSignal);
	if (tile->connection != NULL) {
		{
			std::lock_guard<std::mutex> _(tile->menu->lock);
			auto& connections = tile->menu->connections;
			connections.erase(std::remove(connections.begin(),
				connections.end(), tile->connection), connections.end());
		}
		if (tile->menuRegistration != 0) {
			g_dbus_connection_unregister_object(tile->connection,
				tile->menuRegistration);
		}
		if (tile->registration != 0) {
			g_dbus_connection_unregister_object(tile->connection,
				tile->registration);
		}
		g_dbus_connection_close_sync(tile->connection, NULL, NULL);
		g_object_unref(tile->connection);
	}
	delete tile;
}


static void
emit_new_icon(Tile* tile)
{
	tile->signalPending = false;
	tile->pendingSignal = NULL;
	tile->lastSignal = system_time();
	if (tile->connection != NULL) {
		g_dbus_connection_emit_signal(tile->connection, NULL, kItemPath,
			kItemInterface, "NewIcon", NULL, NULL);
	}
}


// On the GLib thread: NewIcon, at most every kMinIconInterval.
static void
signal_new_icon(Tile* tile)
{
	if (!tile->announced) {
		announce_tile(tile);
		return;
	}
	if (tile->signalPending)
		return;
	bigtime_t wait = tile->lastSignal + kMinIconInterval - system_time();
	if (wait <= 0) {
		emit_new_icon(tile);
		return;
	}
	tile->signalPending = true;
	GSource* source = g_timeout_source_new(wait / 1000 + 1);
	g_source_set_callback(source, [](gpointer data) -> gboolean {
		emit_new_icon((Tile*)data);
		return G_SOURCE_REMOVE;
	}, tile, NULL);
	g_source_attach(source, sContext);
	tile->pendingSignal = source;
	g_source_unref(source);
}


//	#pragma mark - Item


/*!	One Deskbar item: a remote replicant host, shown as tiles. Lives on the
	BApplication thread.
*/
class Item : public BHandler {
public:
								Item(int32 id, const BMessage& archive);
	virtual						~Item();

			int32				ID() const { return fID; }
			const char*			Name() const { return fName.String(); }
			bool				Export();
			void				Check();
			void				Event(BMessage* event);
			void				RequestMenu(int32 tile);
			void				MenuChosen(BMessage* message);

	virtual	void				MessageReceived(BMessage* message);

private:
			void				_Start();
			void				_Started(BMessage* message);
			void				_Disconnect();
			void				_CopyPixels();
			void				_Send(uint32 what, BPoint where,
									BPoint screen, int32 buttons);
			void				_SetMenu(BMessage* message);
			void				_AddNodes(BMessage* archive, int32 parent,
									std::vector<int32> path,
									std::vector<MenuNode>& nodes);

			int32				fID;
			BString				fName;
			BString				fSignature;
			BMessage			fArchive;
			int32				fWidth;
			int32				fHeight;
			std::vector<Tile*>	fTiles;
			std::shared_ptr<MenuModel> fMenu;

			BMessenger			fReplicant;
			area_id				fArea;
			const uint8*		fAreaBase;
			bool				fStarting;
			bigtime_t			fStartTime;
};


Item::Item(int32 id, const BMessage& archive)
	:
	BHandler("tray item"),
	fID(id),
	fName(archive.GetString("_name", "")),
	fSignature(archive.GetString("add_on", "")),
	fArchive(archive),
	fArea(-1),
	fAreaBase(NULL),
	fStarting(false),
	fStartTime(0),
	fMenu(std::make_shared<MenuModel>())
{
	BRect frame = archive.GetRect("_frame", BRect(0, 0, 15, 15));
	fWidth = frame.IntegerWidth() + 1;
	fHeight = frame.IntegerHeight() + 1;

	// Square icons: as many as fit the shape, a wide item being cut where a
	// tile is about as wide as it is tall (48 x 16: two).
	int32 count = (int32)lroundf((float)fWidth / fHeight / 1.5f);
	count = count < 1 ? 1 : count > 4 ? 4 : count;
	BString appName = fSignature;
	app_info info;
	if (be_roster->GetAppInfo(fSignature.String(), &info) == B_OK)
		appName = BPath(&info.ref).Leaf();
	for (int32 i = 0; i < count; i++) {
		Tile* tile = new Tile;
		tile->item = this;
		tile->itemID = id;
		tile->index = i;
		tile->count = count;
		BString tileID;
		tileID.SetToFormat("%s-%s-%" B_PRId32, fSignature.String(),
			fName.String(), i + 1);
		tile->id = tileID.String();
		tile->title = appName.String();
		tile->menu = fMenu;
		fTiles.push_back(tile);
	}
}


Item::~Item()
{
	if (fReplicant.IsValid())
		fReplicant.SendMessage(kMsgRemoteReplicantStop);
	_Disconnect();
	for (Tile* tile : fTiles)
		on_glib([tile]() { unexport_tile(tile); });
}


bool
Item::Export()
{
	// Synchronously, so BDeskbar::AddItem() hears whether the tray took it.
	struct Result {
		std::mutex lock;
		std::condition_variable done;
		bool finished = false;
		bool ok = true;
	};
	auto* result = new Result;
	std::vector<Tile*> tiles = fTiles;
	on_glib([tiles, result]() {
		bool ok = true;
		for (Tile* tile : tiles)
			ok = export_tile(tile) && ok;
		std::lock_guard<std::mutex> _(result->lock);
		result->ok = ok;
		result->finished = true;
		result->done.notify_one();
	});
	bool ok;
	{
		std::unique_lock<std::mutex> lock(result->lock);
		result->done.wait(lock, [result]() { return result->finished; });
		ok = result->ok;
	}
	delete result;
	if (ok)
		_Start();
	return ok;
}


void
Item::Check()
{
	if (fReplicant.IsValid())
		return;
	if (fArea >= 0)
		_Disconnect();		// the app quit: keep the last picture
	if (!fStarting || system_time() - fStartTime > 5 * kCheckInterval)
		_Start();
}


void
Item::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgRemoteReplicantStarted:
			_Started(message);
			break;
		case kMsgRemoteReplicantFailed:
			fprintf(stderr, "TrayExport: %s can't run its item \"%s\": %s\n",
				fSignature.String(), Name(),
				strerror(message->GetInt32("error", B_ERROR)));
			fStarting = false;
			break;
		case kMsgRemoteReplicantPixels:
			_CopyPixels();
			break;
		case kMsgRemoteReplicantMenu:
			_SetMenu(message);
			break;
		default:
			BHandler::MessageReceived(message);
	}
}


void
Item::Event(BMessage* event)
{
	int32 index = event->GetInt32("tile", 0);
	int32 kind = event->GetInt32("kind", kEventActivate);
	if (!fReplicant.IsValid()) {
		if (kind != kEventScroll && !be_roster->IsRunning(fSignature))
			be_roster->Launch(fSignature);
		return;
	}

	// Where the tile is in the replicant: its middle.
	int32 tileWidth = fWidth / (int32)fTiles.size();
	BPoint where(index * tileWidth + tileWidth / 2, fHeight / 2);
	BPoint screen = event->GetPoint("screen", B_ORIGIN);

	if (kind == kEventScroll) {
		BMessage wheel(B_MOUSE_WHEEL_CHANGED);
		float step = event->GetInt32("delta", 0) > 0 ? -1 : 1;
		bool horizontal = event->GetBool("horizontal", false);
		wheel.AddFloat("be:wheel_delta_x", horizontal ? step : 0);
		wheel.AddFloat("be:wheel_delta_y", horizontal ? 0 : step);
		wheel.AddPoint("where", where);
		BMessage forward(kMsgRemoteReplicantEvent);
		forward.AddMessage("event", &wheel);
		forward.AddPoint("screen_origin", screen - where);
		fReplicant.SendMessage(&forward);
		return;
	}

	int32 buttons = kind == kEventActivate ? B_PRIMARY_MOUSE_BUTTON
		: kind == kEventContextMenu ? B_SECONDARY_MOUSE_BUTTON
		: B_TERTIARY_MOUSE_BUTTON;
	_Send(B_MOUSE_DOWN, where, screen, buttons);
	_Send(B_MOUSE_UP, where, screen, 0);
}


void
Item::RequestMenu(int32 index)
{
	// The tray is about to open the menu: a right click on the tile makes
	// the replicant open it, and it comes back as kMsgRemoteReplicantMenu.
	if (!fReplicant.IsValid())
		return;
	int32 tileWidth = fWidth / (int32)fTiles.size();
	BPoint where(index * tileWidth + tileWidth / 2, fHeight / 2);
	_Send(B_MOUSE_DOWN, where, B_ORIGIN, B_SECONDARY_MOUSE_BUTTON);
	_Send(B_MOUSE_UP, where, B_ORIGIN, 0);
}


void
Item::MenuChosen(BMessage* message)
{
	if (!fReplicant.IsValid())
		return;
	BMessage choice(kMsgRemoteReplicantMenuChoice);
	int32 index;
	for (int32 i = 0; message->FindInt32("path", i, &index) == B_OK; i++)
		choice.AddInt32("path", index);
	fReplicant.SendMessage(&choice);
}


void
Item::_AddNodes(BMessage* archive, int32 parent, std::vector<int32> path,
	std::vector<MenuNode>& nodes)
{
	BMessage entry;
	for (int32 i = 0; archive->FindMessage("item", i, &entry) == B_OK; i++) {
		int32 id = (int32)nodes.size();
		nodes.push_back(MenuNode());
		nodes[parent].children.push_back(id);
		MenuNode& node = nodes[id];
		node.path = path;
		node.path.push_back(i);
		node.separator = entry.GetBool("separator", false);
		node.label = entry.GetString("label", "");
		node.enabled = entry.GetBool("enabled", true);
		node.marked = entry.GetBool("marked", false);
		BMessage submenu;
		if (entry.FindMessage("submenu", &submenu) == B_OK)
			_AddNodes(&submenu, id, nodes[id].path, nodes);
	}
}


void
Item::_SetMenu(BMessage* message)
{
	BMessage archive;
	if (message->FindMessage("menu", &archive) != B_OK)
		return;
	std::vector<MenuNode> nodes(1);
	_AddNodes(&archive, 0, std::vector<int32>(), nodes);
	{
		std::lock_guard<std::mutex> _(fMenu->lock);
		fMenu->nodes.swap(nodes);
		fMenu->revision++;
	}
	TRACE("\"%s\" menu: %zu items\n", Name(), fMenu->nodes.size() - 1);
	std::shared_ptr<MenuModel> menu = fMenu;
	on_glib([menu]() { answer_waiting(menu.get(), true); });
}


void
Item::_Send(uint32 what, BPoint where, BPoint screen, int32 buttons)
{
	BMessage event(what);
	event.AddPoint("where", where);
	event.AddInt64("when", system_time());
	event.AddInt32("buttons", buttons);
	event.AddInt32("modifiers", 0);
	if (what == B_MOUSE_DOWN)
		event.AddInt32("clicks", 1);
	BMessage forward(kMsgRemoteReplicantEvent);
	forward.AddMessage("event", &event);
	// The tray says where it was clicked (or 0,0): the replicant's menus
	// open there.
	forward.AddPoint("screen_origin", screen - where);
	fReplicant.SendMessage(&forward);
}


void
Item::_Start()
{
	BMessenger app(fSignature.String());
	TRACE("start \"%s\" in %s: %s\n", Name(), fSignature.String(),
		app.IsValid() ? "running" : "not running");
	if (!app.IsValid())
		return;
	BMessage start(kMsgRemoteReplicantStart);
	start.AddMessage("archive", &fArchive);
	start.AddMessenger("host", BMessenger(this));
	start.AddPoint("screen_origin", B_ORIGIN);
	// Its menus go to the tray as a dbusmenu, which works on Wayland too.
	start.AddBool("export_menus", true);
	if (app.SendMessage(&start, (BHandler*)NULL, 0) == B_OK) {
		fStarting = true;
		fStartTime = system_time();
	}
}


void
Item::_Started(BMessage* message)
{
	_Disconnect();
	fStarting = false;
	void* base = NULL;
	area_id area = clone_area("tray item", &base, B_ANY_ADDRESS, B_READ_AREA,
		message->GetInt32("area", -1));
	if (area < 0)
		return;
	const remote_replicant_header* header
		= (const remote_replicant_header*)base;
	if (header->magic != kRemoteReplicantMagic) {
		delete_area(area);
		return;
	}
	fArea = area;
	fAreaBase = (const uint8*)base;
	fWidth = header->width;
	fHeight = header->height;
	message->FindMessenger("replicant", &fReplicant);
	TRACE("\"%s\" started, %" B_PRId32 "x%" B_PRId32 " in %zu icons\n",
		Name(), fWidth, fHeight, fTiles.size());
	_CopyPixels();
}


void
Item::_Disconnect()
{
	if (fArea >= 0)
		delete_area(fArea);
	fArea = -1;
	fAreaBase = NULL;
	fReplicant = BMessenger();
}


void
Item::_CopyPixels()
{
	if (fAreaBase == NULL)
		return;
	const remote_replicant_header* header
		= (const remote_replicant_header*)fAreaBase;
	const uint8* source = fAreaBase + kRemoteReplicantPixelOffset;
	int32 count = (int32)fTiles.size();
	int32 tileWidth = header->width / count;
	int32 height = header->height;
	int32 size = tileWidth > height ? tileWidth : height;
	int32 top = (size - height) / 2;
	int32 left = (size - tileWidth) / 2;

	for (int32 t = 0; t < count; t++) {
		Tile* tile = fTiles[t];
		std::vector<uint8> pixmap(size * size * 4, 0);
		for (int32 y = 0; y < height; y++) {
			const uint8* row = source + y * header->bytes_per_row
				+ t * tileWidth * 4;
			uint8* out = pixmap.data() + ((y + top) * size + left) * 4;
			const uint8* fullRow = source + y * header->bytes_per_row;
			for (int32 x = 0; x < tileWidth; x++) {
				// Close a cut with the item's own outer edge, so a framed
				// item (the VU meter) gives each tile a whole frame.
				const uint8* pixel = row + x * 4;
				if (x == 0 && t > 0)
					pixel = fullRow;
				else if (x == tileWidth - 1 && t < count - 1)
					pixel = fullRow + (header->width - 1) * 4;
				// B_RGBA32 (B, G, R, A in memory) to ARGB in network order
				out[x * 4 + 0] = pixel[3];
				out[x * 4 + 1] = pixel[2];
				out[x * 4 + 2] = pixel[1];
				out[x * 4 + 3] = pixel[0];
			}
		}
		// Twice the size, pixel for pixel, for trays that draw icons large
		std::vector<uint8> pixmap2x(size * size * 16);
		for (int32 y = 0; y < size * 2; y++) {
			for (int32 x = 0; x < size * 2; x++) {
				memcpy(pixmap2x.data() + (y * size * 2 + x) * 4,
					pixmap.data() + ((y / 2) * size + x / 2) * 4, 4);
			}
		}

		bool changed;
		{
			std::lock_guard<std::mutex> _(tile->lock);
			changed = tile->size != size || tile->pixmap != pixmap;
			if (changed) {
				tile->pixmap.swap(pixmap);
				tile->pixmap2x.swap(pixmap2x);
				tile->size = size;
			}
		}
		if (changed)
			on_glib([tile]() { signal_new_icon(tile); });
	}
}


//	#pragma mark - TrayExportApp


class TrayExportApp : public BApplication {
public:
								TrayExportApp();
	virtual						~TrayExportApp();

	virtual	void				ReadyToRun();
	virtual	void				MessageReceived(BMessage* message);

private:
			Item*				_Find(BMessage* message);
			void				_Add(BMessage* message);
			void				_Remove(Item* item);

			std::map<int32, Item*> fItems;
			int32				fNextID;
			BMessageRunner*		fCheckRunner;
};


TrayExportApp::TrayExportApp()
	:
	BApplication(TRAY_EXPORT_SIGNATURE),
	fNextID(1),
	fCheckRunner(NULL)
{
}


TrayExportApp::~TrayExportApp()
{
	delete fCheckRunner;
	for (auto& entry : fItems) {
		RemoveHandler(entry.second);
		delete entry.second;
	}
}


void
TrayExportApp::ReadyToRun()
{
	BMessage check(kMsgCheck);
	fCheckRunner = new BMessageRunner(BMessenger(this), &check,
		kCheckInterval);
}


Item*
TrayExportApp::_Find(BMessage* message)
{
	int32 id;
	if (message->FindInt32("id", &id) == B_OK) {
		auto found = fItems.find(id);
		return found != fItems.end() ? found->second : NULL;
	}
	const char* name;
	if (message->FindString("name", &name) == B_OK) {
		for (auto& entry : fItems) {
			if (strcmp(entry.second->Name(), name) == 0)
				return entry.second;
		}
	}
	return NULL;
}


void
TrayExportApp::_Add(BMessage* message)
{
	BMessage reply(B_REPLY);
	BMessage archive;
	status_t status = message->FindMessage("view", &archive);
	const char* signature = archive.GetString("add_on", NULL);
	if (status == B_OK && signature == NULL)
		status = B_BAD_VALUE;
	// TrayBridge's items are the tray's own icons already.
	if (status == B_OK && strcasecmp(signature, TRAY_BRIDGE_SIGNATURE) == 0)
		status = B_NOT_ALLOWED;

	if (status == B_OK) {
		int32 id = fNextID++;
		Item* item = new Item(id, archive);
		AddHandler(item);
		if (item->Export()) {
			fItems[id] = item;
			reply.AddInt32("id", id);
		} else {
			RemoveHandler(item);
			delete item;
			status = B_NAME_NOT_FOUND;
		}
	}
	reply.AddInt32("error", status);
	message->SendReply(&reply);
	TRACE("add \"%s\": %s\n", archive.GetString("_name", "?"),
		strerror(status));
}


void
TrayExportApp::_Remove(Item* item)
{
	TRACE("remove \"%s\"\n", item->Name());
	fItems.erase(item->ID());
	RemoveHandler(item);
	delete item;
	if (fItems.empty())
		PostMessage(B_QUIT_REQUESTED);
}


void
TrayExportApp::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgAddView:
			_Add(message);
			break;

		case kMsgHasItem:
		{
			BMessage reply(B_REPLY);
			reply.AddBool("exists", _Find(message) != NULL);
			message->SendReply(&reply);
			break;
		}

		case kMsgGetItemInfo:
		{
			BMessage reply(B_REPLY);
			Item* item = _Find(message);
			if (item != NULL) {
				reply.AddInt32("id", item->ID());
				reply.AddString("name", item->Name());
			}
			reply.AddInt32("error", item != NULL ? B_OK : B_NAME_NOT_FOUND);
			message->SendReply(&reply);
			break;
		}

		case kMsgCountItems:
		{
			BMessage reply(B_REPLY);
			reply.AddInt32("count", (int32)fItems.size());
			message->SendReply(&reply);
			break;
		}

		case kMsgMaxItemSize:
		{
			BMessage reply(B_REPLY);
			reply.AddFloat("width", 256);
			reply.AddFloat("height", 64);
			message->SendReply(&reply);
			break;
		}

		case kMsgRemoveItem:
			if (Item* item = _Find(message))
				_Remove(item);
			break;

		case kMsgMenuRequest:
		{
			auto found = fItems.find(message->GetInt32("item", -1));
			if (found != fItems.end())
				found->second->RequestMenu(message->GetInt32("tile", 0));
			break;
		}

		case kMsgMenuChosen:
		{
			auto found = fItems.find(message->GetInt32("item", -1));
			if (found != fItems.end())
				found->second->MenuChosen(message);
			break;
		}

		case kMsgTrayEvent:
		{
			auto found = fItems.find(message->GetInt32("item", -1));
			if (found != fItems.end())
				found->second->Event(message);
			break;
		}

		case kMsgCheck:
			for (auto& entry : fItems)
				entry.second->Check();
			break;

		default:
			BApplication::MessageReceived(message);
	}
}


int
main()
{
	GError* error = NULL;
	gchar* address = g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SESSION,
		NULL, &error);
	if (address == NULL) {
		fprintf(stderr, "TrayExport: no session bus: %s\n", error->message);
		g_error_free(error);
		return 1;
	}
	sBusAddress = address;
	g_free(address);
	sNodeInfo = g_dbus_node_info_new_for_xml(kIntrospection, NULL);
	sContext = g_main_context_new();
	sLoop = g_main_loop_new(sContext, FALSE);
	GThread* thread = g_thread_new("TrayExport D-Bus", [](gpointer) -> gpointer {
		g_main_context_push_thread_default(sContext);
		g_main_loop_run(sLoop);
		return NULL;
	}, NULL);

	TrayExportApp app;
	app.Run();

	g_main_loop_quit(sLoop);
	g_thread_join(thread);
	return 0;
}
