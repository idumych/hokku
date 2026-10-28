/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 *
 * TrayBridge: shows the Linux desktop's tray icons in the Deskbar tray.
 *
 * It is the StatusNotifierWatcher (org.kde and org.freedesktop names) and a
 * StatusNotifierHost on the session bus, as the KDE and Xfce panels are, so
 * apps using KStatusNotifierItem, libappindicator / libayatana-appindicator,
 * Qt's QSystemTrayIcon, Electron's Tray (IVPN, Signal, Slack...) and so on
 * register their icons here. When another watcher already runs (inside a
 * Plasma session, say) it only registers as a host there.
 *
 * Each item becomes a TrayBridgeItem replicant, added with
 * BDeskbar::AddItem(). The replicant sends clicks, wheel turns and menu
 * choices back here; icon, tooltip and status changes are pushed to it.
 * Legacy XEmbed icons reach us through xembedsniproxy or snixembed, which
 * turn them into StatusNotifierItems; TrayBridge starts one if installed.
 *
 * All D-Bus work and all item state live on a GLib main loop thread. The
 * BApplication thread only hands incoming BMessages over to it.
 */


#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <map>
#include <string>

#include <gio/gio.h>

#include <Application.h>
#include <Bitmap.h>
#include <Deskbar.h>
#include <View.h>

#include "DBusMenu.h"
#include "IconLoader.h"
#include "TrayBridgeProtocol.h"


static const char* kWatcherPath = "/StatusNotifierWatcher";
static const char* kKDEWatcher = "org.kde.StatusNotifierWatcher";
static const char* kFreedesktopWatcher
	= "org.freedesktop.StatusNotifierWatcher";
static const char* kKDEItem = "org.kde.StatusNotifierItem";
static const char* kFreedesktopItem = "org.freedesktop.StatusNotifierItem";
static const char* kDefaultItemPath = "/StatusNotifierItem";

static const char* kWatcherXML =
	"<node>"
	"  <interface name='%s'>"
	"    <method name='RegisterStatusNotifierItem'>"
	"      <arg name='service' type='s' direction='in'/>"
	"    </method>"
	"    <method name='RegisterStatusNotifierHost'>"
	"      <arg name='service' type='s' direction='in'/>"
	"    </method>"
	"    <property name='RegisteredStatusNotifierItems' type='as'"
	"      access='read'/>"
	"    <property name='IsStatusNotifierHostRegistered' type='b'"
	"      access='read'/>"
	"    <property name='ProtocolVersion' type='i' access='read'/>"
	"    <signal name='StatusNotifierItemRegistered'>"
	"      <arg type='s'/>"
	"    </signal>"
	"    <signal name='StatusNotifierItemUnregistered'>"
	"      <arg type='s'/>"
	"    </signal>"
	"    <signal name='StatusNotifierHostRegistered'/>"
	"    <signal name='StatusNotifierHostUnregistered'/>"
	"  </interface>"
	"</node>";


static int
default_icon_size()
{
	const char* env = getenv("HOKKU_TRAY_ICON_SIZE");
	int size = env != NULL ? atoi(env) : 0;
	return size >= 8 && size <= 128 ? size : 16;
}


static std::string
strip_markup(const char* text)
{
	// Tooltips may carry a subset of HTML.
	std::string result;
	bool inTag = false;
	for (const char* c = text; *c != '\0'; c++) {
		if (*c == '<') {
			if (strncasecmp(c, "<br", 3) == 0)
				result += '\n';
			inTag = true;
			continue;
		}
		if (*c == '>' && inTag) {
			inTag = false;
			continue;
		}
		if (inTag)
			continue;
		if (*c == '&') {
			struct { const char* entity; char replacement; } entities[] = {
				{ "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' },
				{ "&quot;", '"' }, { "&apos;", '\'' }, { "&nbsp;", ' ' },
			};
			bool replaced = false;
			for (auto& entity : entities) {
				size_t length = strlen(entity.entity);
				if (strncmp(c, entity.entity, length) == 0) {
					result += entity.replacement;
					c += length - 1;
					replaced = true;
					break;
				}
			}
			if (replaced)
				continue;
		}
		result += *c;
	}
	return result;
}


// #pragma mark - TrayItem


struct TrayItem {
	std::string		key;			// service + path
	std::string		service;
	std::string		path;
	const char*		interface = kKDEItem;

	std::string		id;
	std::string		title;
	std::string		status = "Active";
	std::string		iconName;
	std::string		attentionIconName;
	std::string		overlayIconName;
	std::string		iconThemePath;
	GVariant*		iconPixmap = NULL;
	GVariant*		attentionIconPixmap = NULL;
	GVariant*		overlayIconPixmap = NULL;
	std::string		toolTip;
	std::string		menuPath;
	bool			itemIsMenu = false;
	bool			loaded = false;

	int32			deskbarID = -1;
	BMessenger		view;
	int				iconSize = default_icon_size();

	guint			nameWatch = 0;
	guint			signalSubscription = 0;
	guint			refreshSource = 0;

	~TrayItem()
	{
		if (iconPixmap != NULL)
			g_variant_unref(iconPixmap);
		if (attentionIconPixmap != NULL)
			g_variant_unref(attentionIconPixmap);
		if (overlayIconPixmap != NULL)
			g_variant_unref(overlayIconPixmap);
	}

	bool Visible() const
	{
		if (!loaded)
			return false;
		if (status != "Passive")
			return true;
		const char* showPassive = getenv("HOKKU_TRAY_SHOW_PASSIVE");
		return showPassive != NULL && strcmp(showPassive, "0") != 0;
	}
};


/*!	The view TrayBridge hands to BDeskbar::AddItem(): it is only ever
	archived, as a TrayBridgeItem, which Deskbar instantiates.
*/
class ItemArchiveView : public BView {
public:
	ItemArchiveView(const TrayItem* item, BBitmap* icon)
		:
		BView(BRect(0, 0, item->iconSize - 1, item->iconSize - 1),
			(std::string(TRAY_BRIDGE_VIEW_PREFIX) + item->key).c_str(),
			B_FOLLOW_LEFT | B_FOLLOW_TOP, B_WILL_DRAW),
		fItem(item),
		fIcon(icon)
	{
	}

	virtual status_t Archive(BMessage* into, bool deep = true) const
	{
		status_t status = BView::Archive(into, deep);
		if (status != B_OK)
			return status;

		into->RemoveName("class");
		into->AddString("class", TRAY_BRIDGE_ITEM_CLASS);
		into->AddString("add_on", TRAY_BRIDGE_SIGNATURE);
		into->AddString(TB_FIELD_ITEM, fItem->key.c_str());
		into->AddMessenger(TB_FIELD_BRIDGE, be_app_messenger);
		into->AddString(TB_FIELD_TOOLTIP, fItem->toolTip.c_str());
		if (fIcon != NULL) {
			BMessage icon;
			if (fIcon->Archive(&icon) == B_OK)
				into->AddMessage(TB_FIELD_ICON, &icon);
		}
		return B_OK;
	}

private:
	const TrayItem*	fItem;
	BBitmap*		fIcon;
};


// #pragma mark - Bridge


class Bridge {
public:
							Bridge();

			void			Start();
			void			Stop();

			// called on the GLib thread with messages from replicants
			void			HandleMessage(BMessage* message);

			void			DeskbarStarted();

private:
	static	void			_BusAcquired(GObject* source, GAsyncResult* result,
								gpointer data);
	static	void			_WatcherMethod(GDBusConnection* connection,
								const gchar* sender, const gchar* path,
								const gchar* interface, const gchar* method,
								GVariant* parameters,
								GDBusMethodInvocation* invocation,
								gpointer data);
	static	GVariant*		_WatcherProperty(GDBusConnection* connection,
								const gchar* sender, const gchar* path,
								const gchar* interface,
								const gchar* property, GError** error,
								gpointer data);
	static	void			_NameAcquired(GDBusConnection* connection,
								const gchar* name, gpointer data);
	static	void			_NameLost(GDBusConnection* connection,
								const gchar* name, gpointer data);
	static	void			_ItemVanished(GDBusConnection* connection,
								const gchar* name, gpointer data);
	static	void			_ItemSignal(GDBusConnection* connection,
								const gchar* sender, const gchar* path,
								const gchar* interface, const gchar* signal,
								GVariant* parameters, gpointer data);
	static	void			_ExternalWatcherSignal(
								GDBusConnection* connection,
								const gchar* sender, const gchar* path,
								const gchar* interface, const gchar* signal,
								GVariant* parameters, gpointer data);
	static	gboolean		_RefreshTimeout(gpointer data);
	static	void			_PropertiesReceived(GObject* source,
								GAsyncResult* result, gpointer data);
	static	void			_ActivateDone(GObject* source,
								GAsyncResult* result, gpointer data);
	static	gboolean		_ReAddTimeout(gpointer data);

			void			_ExportWatcher(const char* interface);
			void			_EmitWatcherSignal(const char* signal,
								const char* argument);
			void			_UseExternalWatcher();
			void			_AddRegistration(const char* spec,
								const char* sender);
			TrayItem*		_AddItem(const std::string& service,
								const std::string& path);
			void			_RemoveItem(const std::string& key);
			TrayItem*		_Find(const char* key);
			void			_ScheduleRefresh(TrayItem* item);
			void			_Refresh(TrayItem* item);
			void			_ApplyProperties(TrayItem* item,
								GVariant* properties);
			BBitmap*		_RenderIcon(TrayItem* item);
			void			_Sync(TrayItem* item, bool iconChanged);
			void			_AddToDeskbar(TrayItem* item);
			void			_RemoveFromDeskbar(TrayItem* item);
			void			_PushUpdate(TrayItem* item);
			void			_ShowMenu(TrayItem* item, int x, int y);
			void			_CallItem(TrayItem* item, const char* method,
								GVariant* parameters,
								GAsyncReadyCallback callback = NULL,
								gpointer data = NULL);
			void			_StartXEmbedProxy();

			GDBusConnection*	fConnection;
			std::map<std::string, TrayItem*> fItems;
			guint			fWatcherOwners[2];
			guint			fHostOwner;
			guint			fRegistrations[2];
			bool			fOwnsWatcher;
			bool			fExternalWatcher;
			guint			fExternalSubscriptions[2];
			GPid			fXEmbedProxy;
};


static Bridge* sBridge = NULL;


Bridge::Bridge()
	:
	fConnection(NULL),
	fHostOwner(0),
	fOwnsWatcher(false),
	fExternalWatcher(false),
	fXEmbedProxy(0)
{
	fWatcherOwners[0] = fWatcherOwners[1] = 0;
	fRegistrations[0] = fRegistrations[1] = 0;
	fExternalSubscriptions[0] = fExternalSubscriptions[1] = 0;
}


void
Bridge::Start()
{
	g_bus_get(G_BUS_TYPE_SESSION, NULL, &_BusAcquired, this);
}


void
Bridge::Stop()
{
	for (auto& entry : fItems) {
		_RemoveFromDeskbar(entry.second);
		delete entry.second;
	}
	fItems.clear();

	if (fXEmbedProxy != 0) {
		kill(fXEmbedProxy, SIGTERM);
		g_spawn_close_pid(fXEmbedProxy);
		fXEmbedProxy = 0;
	}
	if (fConnection != NULL)
		g_dbus_connection_flush_sync(fConnection, NULL, NULL);
}


void
Bridge::_BusAcquired(GObject* source, GAsyncResult* result, gpointer data)
{
	Bridge* self = (Bridge*)data;
	GError* error = NULL;
	self->fConnection = g_bus_get_finish(result, &error);
	if (self->fConnection == NULL) {
		fprintf(stderr, "TrayBridge: no session bus: %s\n", error->message);
		g_error_free(error);
		be_app->PostMessage(B_QUIT_REQUESTED);
		return;
	}

	self->_ExportWatcher(kKDEWatcher);
	self->_ExportWatcher(kFreedesktopWatcher);

	// Don't take the watcher from a running panel: if it's taken, just
	// register with it as a host.
	self->fWatcherOwners[0] = g_bus_own_name_on_connection(self->fConnection,
		kKDEWatcher, G_BUS_NAME_OWNER_FLAGS_NONE, &_NameAcquired, &_NameLost,
		self, NULL);
	self->fWatcherOwners[1] = g_bus_own_name_on_connection(self->fConnection,
		kFreedesktopWatcher, G_BUS_NAME_OWNER_FLAGS_NONE, NULL, NULL, NULL,
		NULL);

	char hostName[64];
	snprintf(hostName, sizeof(hostName), "org.kde.StatusNotifierHost-%d",
		(int)getpid());
	self->fHostOwner = g_bus_own_name_on_connection(self->fConnection,
		hostName, G_BUS_NAME_OWNER_FLAGS_NONE, NULL, NULL, NULL, NULL);

	self->_StartXEmbedProxy();
}


void
Bridge::_ExportWatcher(const char* interface)
{
	gchar* xml = g_strdup_printf(kWatcherXML, interface);
	GDBusNodeInfo* info = g_dbus_node_info_new_for_xml(xml, NULL);
	g_free(xml);
	if (info == NULL)
		return;

	static const GDBusInterfaceVTable vtable = {
		&_WatcherMethod, &_WatcherProperty, NULL, { 0 }
	};
	int index = strcmp(interface, kKDEWatcher) == 0 ? 0 : 1;
	fRegistrations[index] = g_dbus_connection_register_object(fConnection,
		kWatcherPath, info->interfaces[0], &vtable, this, NULL, NULL);
	g_dbus_node_info_unref(info);
}


void
Bridge::_NameAcquired(GDBusConnection* connection, const gchar* name,
	gpointer data)
{
	Bridge* self = (Bridge*)data;
	self->fOwnsWatcher = true;
	self->_EmitWatcherSignal("StatusNotifierHostRegistered", NULL);
}


void
Bridge::_NameLost(GDBusConnection* connection, const gchar* name,
	gpointer data)
{
	Bridge* self = (Bridge*)data;
	if (connection == NULL)
		return;
	if (!self->fOwnsWatcher)
		self->_UseExternalWatcher();
	self->fOwnsWatcher = false;
}


void
Bridge::_EmitWatcherSignal(const char* signal, const char* argument)
{
	const char* interfaces[] = { kKDEWatcher, kFreedesktopWatcher };
	for (const char* interface : interfaces) {
		g_dbus_connection_emit_signal(fConnection, NULL, kWatcherPath,
			interface, signal,
			argument != NULL ? g_variant_new("(s)", argument) : NULL, NULL);
	}
}


void
Bridge::_WatcherMethod(GDBusConnection* connection, const gchar* sender,
	const gchar* path, const gchar* interface, const gchar* method,
	GVariant* parameters, GDBusMethodInvocation* invocation, gpointer data)
{
	Bridge* self = (Bridge*)data;

	if (strcmp(method, "RegisterStatusNotifierItem") == 0) {
		const char* spec;
		g_variant_get(parameters, "(&s)", &spec);
		self->_AddRegistration(spec, sender);
		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (strcmp(method, "RegisterStatusNotifierHost") == 0) {
		// other hosts are welcome, nothing to keep
		g_dbus_method_invocation_return_value(invocation, NULL);
	} else {
		g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
			G_DBUS_ERROR_UNKNOWN_METHOD, "Unknown method %s", method);
	}
}


GVariant*
Bridge::_WatcherProperty(GDBusConnection* connection, const gchar* sender,
	const gchar* path, const gchar* interface, const gchar* property,
	GError** error, gpointer data)
{
	Bridge* self = (Bridge*)data;

	if (strcmp(property, "RegisteredStatusNotifierItems") == 0) {
		GVariantBuilder builder;
		g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
		for (auto& entry : self->fItems)
			g_variant_builder_add(&builder, "s", entry.first.c_str());
		return g_variant_builder_end(&builder);
	}
	if (strcmp(property, "IsStatusNotifierHostRegistered") == 0)
		return g_variant_new_boolean(TRUE);
	if (strcmp(property, "ProtocolVersion") == 0)
		return g_variant_new_int32(0);

	g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
		"Unknown property %s", property);
	return NULL;
}


/*!	Another watcher (a Plasma or Xfce panel) owns the name: take part as a
	host. Its registered items are "service/path" or plain service names.
*/
void
Bridge::_UseExternalWatcher()
{
	if (fExternalWatcher)
		return;
	fExternalWatcher = true;
	fprintf(stderr, "TrayBridge: another StatusNotifierWatcher is running, "
		"using it\n");

	fExternalSubscriptions[0] = g_dbus_connection_signal_subscribe(
		fConnection, kKDEWatcher, kKDEWatcher, NULL, kWatcherPath, NULL,
		G_DBUS_SIGNAL_FLAGS_NONE, &_ExternalWatcherSignal, this, NULL);

	char hostName[64];
	snprintf(hostName, sizeof(hostName), "org.kde.StatusNotifierHost-%d",
		(int)getpid());
	g_dbus_connection_call(fConnection, kKDEWatcher, kWatcherPath,
		kKDEWatcher, "RegisterStatusNotifierHost",
		g_variant_new("(s)", hostName), NULL, G_DBUS_CALL_FLAGS_NONE, -1,
		NULL, NULL, NULL);

	GVariant* result = g_dbus_connection_call_sync(fConnection, kKDEWatcher,
		kWatcherPath, "org.freedesktop.DBus.Properties", "Get",
		g_variant_new("(ss)", kKDEWatcher, "RegisteredStatusNotifierItems"),
		G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, 2000, NULL, NULL);
	if (result == NULL)
		return;

	GVariant* boxed;
	g_variant_get(result, "(v)", &boxed);
	GVariantIter iter;
	const char* spec;
	g_variant_iter_init(&iter, boxed);
	while (g_variant_iter_next(&iter, "&s", &spec))
		_AddRegistration(spec, NULL);
	g_variant_unref(boxed);
	g_variant_unref(result);
}


void
Bridge::_ExternalWatcherSignal(GDBusConnection* connection,
	const gchar* sender, const gchar* path, const gchar* interface,
	const gchar* signal, GVariant* parameters, gpointer data)
{
	Bridge* self = (Bridge*)data;
	if (!g_variant_is_of_type(parameters, G_VARIANT_TYPE("(s)")))
		return;

	const char* spec;
	g_variant_get(parameters, "(&s)", &spec);
	if (strcmp(signal, "StatusNotifierItemRegistered") == 0)
		self->_AddRegistration(spec, NULL);
	else if (strcmp(signal, "StatusNotifierItemUnregistered") == 0) {
		const char* slash = strchr(spec, '/');
		std::string key = slash != NULL ? spec
			: std::string(spec) + kDefaultItemPath;
		self->_RemoveItem(key);
	}
}


/*!	RegisterStatusNotifierItem's argument is a bus name (the item is at
	/StatusNotifierItem), an object path (libappindicator: the item belongs
	to the caller), or, from another watcher, "service/path".
*/
void
Bridge::_AddRegistration(const char* spec, const char* sender)
{
	std::string service;
	std::string path;
	if (spec[0] == '/') {
		if (sender == NULL)
			return;
		service = sender;
		path = spec;
	} else {
		const char* slash = strchr(spec, '/');
		if (slash != NULL) {
			service.assign(spec, slash - spec);
			path = slash;
		} else {
			service = spec;
			path = kDefaultItemPath;
		}
	}

	if (_AddItem(service, path) != NULL)
		_EmitWatcherSignal("StatusNotifierItemRegistered",
			(service + path).c_str());
}


TrayItem*
Bridge::_AddItem(const std::string& service, const std::string& path)
{
	std::string key = service + path;
	if (fItems.find(key) != fItems.end())
		return NULL;

	TrayItem* item = new TrayItem;
	item->key = key;
	item->service = service;
	item->path = path;
	fItems[key] = item;

	item->nameWatch = g_bus_watch_name_on_connection(fConnection,
		service.c_str(), G_BUS_NAME_WATCHER_FLAGS_NONE, NULL, &_ItemVanished,
		g_strdup(key.c_str()), g_free);

	item->signalSubscription = g_dbus_connection_signal_subscribe(fConnection,
		service.c_str(), NULL, NULL, path.c_str(), NULL,
		G_DBUS_SIGNAL_FLAGS_NONE, &_ItemSignal, g_strdup(key.c_str()),
		g_free);

	_Refresh(item);
	return item;
}


void
Bridge::_RemoveItem(const std::string& key)
{
	auto found = fItems.find(key);
	if (found == fItems.end())
		return;

	TrayItem* item = found->second;
	fItems.erase(found);

	_RemoveFromDeskbar(item);
	if (item->nameWatch != 0)
		g_bus_unwatch_name(item->nameWatch);
	if (item->signalSubscription != 0)
		g_dbus_connection_signal_unsubscribe(fConnection,
			item->signalSubscription);
	if (item->refreshSource != 0)
		g_source_remove(item->refreshSource);

	if (fOwnsWatcher)
		_EmitWatcherSignal("StatusNotifierItemUnregistered", key.c_str());
	delete item;
}


TrayItem*
Bridge::_Find(const char* key)
{
	if (key == NULL)
		return NULL;
	auto found = fItems.find(key);
	return found != fItems.end() ? found->second : NULL;
}


void
Bridge::_ItemVanished(GDBusConnection* connection, const gchar* name,
	gpointer data)
{
	sBridge->_RemoveItem((const char*)data);
}


void
Bridge::_ItemSignal(GDBusConnection* connection, const gchar* sender,
	const gchar* path, const gchar* interface, const gchar* signal,
	GVariant* parameters, gpointer data)
{
	TrayItem* item = sBridge->_Find((const char*)data);
	if (item == NULL)
		return;

	// NewIcon, NewAttentionIcon, NewOverlayIcon, NewToolTip, NewTitle,
	// NewStatus, NewIconThemePath, NewMenu, PropertiesChanged: refetch
	// everything, coalescing bursts.
	if (g_str_has_prefix(signal, "New")
		|| strcmp(signal, "PropertiesChanged") == 0) {
		sBridge->_ScheduleRefresh(item);
	}
}


void
Bridge::_ScheduleRefresh(TrayItem* item)
{
	if (item->refreshSource != 0)
		return;
	item->refreshSource = g_timeout_add_full(G_PRIORITY_DEFAULT, 40,
		&_RefreshTimeout, g_strdup(item->key.c_str()), g_free);
}


gboolean
Bridge::_RefreshTimeout(gpointer data)
{
	TrayItem* item = sBridge->_Find((const char*)data);
	if (item != NULL) {
		item->refreshSource = 0;
		sBridge->_Refresh(item);
	}
	return G_SOURCE_REMOVE;
}


struct RefreshRequest {
	std::string	key;
	const char*	interface;
};


void
Bridge::_Refresh(TrayItem* item)
{
	RefreshRequest* request = new RefreshRequest;
	request->key = item->key;
	request->interface = item->interface;

	g_dbus_connection_call(fConnection, item->service.c_str(),
		item->path.c_str(), "org.freedesktop.DBus.Properties", "GetAll",
		g_variant_new("(s)", item->interface), G_VARIANT_TYPE("(a{sv})"),
		G_DBUS_CALL_FLAGS_NONE, 3000, NULL, &_PropertiesReceived, request);
}


void
Bridge::_PropertiesReceived(GObject* source, GAsyncResult* result,
	gpointer data)
{
	RefreshRequest* request = (RefreshRequest*)data;
	GError* error = NULL;
	GVariant* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source),
		result, &error);

	TrayItem* item = sBridge->_Find(request->key.c_str());
	if (item == NULL) {
		if (reply != NULL)
			g_variant_unref(reply);
		if (error != NULL)
			g_error_free(error);
		delete request;
		return;
	}

	if (reply == NULL) {
		g_error_free(error);
		if (request->interface == kKDEItem && !item->loaded) {
			// some implementations use the freedesktop name
			item->interface = kFreedesktopItem;
			sBridge->_Refresh(item);
		}
		delete request;
		return;
	}

	GVariant* properties = g_variant_get_child_value(reply, 0);
	sBridge->_ApplyProperties(item, properties);
	g_variant_unref(properties);
	g_variant_unref(reply);
	delete request;
}


static void
replace_variant(GVariant*& field, GVariant* properties, const char* name)
{
	GVariant* value = g_variant_lookup_value(properties, name,
		G_VARIANT_TYPE("a(iiay)"));
	if (field != NULL)
		g_variant_unref(field);
	field = value;
}


static std::string
lookup_string(GVariant* properties, const char* name)
{
	const char* value = NULL;
	if (g_variant_lookup(properties, name, "&s", &value)
		|| g_variant_lookup(properties, name, "&o", &value)) {
		return value;
	}
	return "";
}


void
Bridge::_ApplyProperties(TrayItem* item, GVariant* properties)
{
	std::string oldStatus = item->status;
	std::string oldToolTip = item->toolTip;

	item->id = lookup_string(properties, "Id");
	item->title = lookup_string(properties, "Title");
	item->status = lookup_string(properties, "Status");
	if (item->status.empty())
		item->status = "Active";
	item->iconName = lookup_string(properties, "IconName");
	item->attentionIconName = lookup_string(properties, "AttentionIconName");
	item->overlayIconName = lookup_string(properties, "OverlayIconName");
	item->iconThemePath = lookup_string(properties, "IconThemePath");
	item->menuPath = lookup_string(properties, "Menu");
	if (item->menuPath == "/")
		item->menuPath.clear();
	replace_variant(item->iconPixmap, properties, "IconPixmap");
	replace_variant(item->attentionIconPixmap, properties,
		"AttentionIconPixmap");
	replace_variant(item->overlayIconPixmap, properties, "OverlayIconPixmap");

	gboolean isMenu = FALSE;
	g_variant_lookup(properties, "ItemIsMenu", "b", &isMenu);
	item->itemIsMenu = isMenu;

	// ToolTip: (icon name, icon pixmaps, title, description)
	std::string toolTip;
	GVariant* toolTipValue = g_variant_lookup_value(properties, "ToolTip",
		G_VARIANT_TYPE("(sa(iiay)ss)"));
	if (toolTipValue != NULL) {
		const char* title;
		const char* description;
		g_variant_get(toolTipValue, "(&s@a(iiay)&s&s)", NULL, NULL, &title,
			&description);
		toolTip = strip_markup(title);
		std::string text = strip_markup(description);
		if (!text.empty()) {
			if (!toolTip.empty())
				toolTip += "\n";
			toolTip += text;
		}
		g_variant_unref(toolTipValue);
	}
	if (toolTip.empty())
		toolTip = item->title;
	item->toolTip = toolTip;

	item->loaded = true;
	_Sync(item, true);
}


BBitmap*
Bridge::_RenderIcon(TrayItem* item)
{
	int size = item->iconSize;
	const char* themePath = item->iconThemePath.c_str();

	cairo_surface_t* icon = NULL;
	if (item->status == "NeedsAttention") {
		icon = IconLoader::LoadNamed(item->attentionIconName.c_str(), size,
			themePath);
		if (icon == NULL)
			icon = IconLoader::LoadPixmaps(item->attentionIconPixmap, size);
	}
	if (icon == NULL)
		icon = IconLoader::LoadNamed(item->iconName.c_str(), size, themePath);
	if (icon == NULL)
		icon = IconLoader::LoadPixmaps(item->iconPixmap, size);
	if (icon == NULL) {
		// something rather than an empty gap
		icon = IconLoader::LoadNamed("application-x-executable", size);
	}
	if (icon == NULL)
		return NULL;

	cairo_surface_t* overlay = IconLoader::LoadNamed(
		item->overlayIconName.c_str(), size, themePath);
	if (overlay == NULL)
		overlay = IconLoader::LoadPixmaps(item->overlayIconPixmap, size);

	BBitmap* bitmap = IconLoader::MakeBitmap(icon, size, overlay);
	cairo_surface_destroy(icon);
	if (overlay != NULL)
		cairo_surface_destroy(overlay);
	return bitmap;
}


void
Bridge::_Sync(TrayItem* item, bool iconChanged)
{
	if (!item->Visible()) {
		_RemoveFromDeskbar(item);
		return;
	}
	if (item->deskbarID < 0) {
		_AddToDeskbar(item);
		return;
	}
	if (iconChanged)
		_PushUpdate(item);
}


void
Bridge::_AddToDeskbar(TrayItem* item)
{
	BBitmap* icon = _RenderIcon(item);
	ItemArchiveView view(item, icon);

	BDeskbar deskbar;
	int32 id = -1;
	status_t status = deskbar.AddItem(&view, &id);
	delete icon;

	if (status != B_OK) {
		fprintf(stderr, "TrayBridge: couldn't add %s to the Deskbar: %s\n",
			item->key.c_str(), strerror(status));
		return;
	}
	item->deskbarID = id;
	item->view = BMessenger();
		// set again when the replicant registers
}


void
Bridge::_RemoveFromDeskbar(TrayItem* item)
{
	if (item->deskbarID < 0)
		return;

	// By name, not ID: IDs start over with each Deskbar, so a stale one
	// could take out another replicant.
	BDeskbar deskbar;
	deskbar.RemoveItem(
		(std::string(TRAY_BRIDGE_VIEW_PREFIX) + item->key).c_str());
	item->deskbarID = -1;
	item->view = BMessenger();
}


void
Bridge::_PushUpdate(TrayItem* item)
{
	if (!item->view.IsValid())
		return;

	BMessage update(kTBUpdate);
	BBitmap* icon = _RenderIcon(item);
	if (icon != NULL) {
		BMessage archive;
		if (icon->Archive(&archive) == B_OK)
			update.AddMessage(TB_FIELD_ICON, &archive);
		delete icon;
	}
	update.AddString(TB_FIELD_TOOLTIP, item->toolTip.c_str());
	item->view.SendMessage(&update);
}


void
Bridge::_CallItem(TrayItem* item, const char* method, GVariant* parameters,
	GAsyncReadyCallback callback, gpointer data)
{
	g_dbus_connection_call(fConnection, item->service.c_str(),
		item->path.c_str(), item->interface, method, parameters, NULL,
		G_DBUS_CALL_FLAGS_NONE, 3000, NULL, callback, data);
}


struct ActivateRequest {
	std::string	key;
	int			x;
	int			y;
};


/*!	Activate failed (not implemented, or ItemIsMenu set but the app
	answers anyway with an error): show the menu, as Plasma does.
*/
void
Bridge::_ActivateDone(GObject* source, GAsyncResult* result, gpointer data)
{
	ActivateRequest* request = (ActivateRequest*)data;
	GError* error = NULL;
	GVariant* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source),
		result, &error);
	if (reply != NULL)
		g_variant_unref(reply);
	else {
		g_error_free(error);
		TrayItem* item = sBridge->_Find(request->key.c_str());
		if (item != NULL)
			sBridge->_ShowMenu(item, request->x, request->y);
	}
	delete request;
}


void
Bridge::_ShowMenu(TrayItem* item, int x, int y)
{
	if (!item->menuPath.empty() && item->view.IsValid()) {
		DBusMenu::ShowAsync(fConnection, item->service, item->menuPath,
			item->iconThemePath, x, y, item->view);
	} else
		_CallItem(item, "ContextMenu", g_variant_new("(ii)", x, y));
}


void
Bridge::HandleMessage(BMessage* message)
{
	TrayItem* item = _Find(message->GetString(TB_FIELD_ITEM, NULL));
	if (item == NULL)
		return;

	int32 x = message->GetInt32("x", 0);
	int32 y = message->GetInt32("y", 0);

	switch (message->what) {
		case kTBRegisterView:
		{
			BMessenger view;
			if (message->FindMessenger("view", &view) != B_OK)
				break;
			item->view = view;

			int32 size = message->GetInt32("size", item->iconSize);
			bool resized = size >= 8 && size <= 128 && size != item->iconSize;
			if (resized)
				item->iconSize = size;
			// The archive carried the icon of the moment it was added;
			// whatever changed since (or the size) is sent now.
			_PushUpdate(item);
			break;
		}

		case kTBActivate:
			if (message->GetInt32("button", 1) == 2) {
				_CallItem(item, "SecondaryActivate",
					g_variant_new("(ii)", x, y));
			} else if (item->itemIsMenu && !item->menuPath.empty()) {
				_ShowMenu(item, x, y);
			} else {
				ActivateRequest* request = new ActivateRequest;
				request->key = item->key;
				request->x = x;
				request->y = y;
				_CallItem(item, "Activate", g_variant_new("(ii)", x, y),
					&_ActivateDone, request);
			}
			break;

		case kTBMenuRequest:
			_ShowMenu(item, x, y);
			break;

		case kTBMenuEvent:
		{
			if (item->menuPath.empty())
				break;
			int32 id;
			if (message->FindInt32("id", &id) == B_OK) {
				DBusMenu::SendClicked(fConnection, item->service,
					item->menuPath, id);
			} else {
				DBusMenu::SendClosed(fConnection, item->service,
					item->menuPath);
			}
			break;
		}

		case kTBScroll:
			_CallItem(item, "Scroll", g_variant_new("(is)",
				message->GetInt32("delta", 0),
				message->GetString("orientation", "vertical")));
			break;
	}
}


gboolean
Bridge::_ReAddTimeout(gpointer data)
{
	for (auto& entry : sBridge->fItems)
		sBridge->_Sync(entry.second, true);
	return G_SOURCE_REMOVE;
}


void
Bridge::DeskbarStarted()
{
	// Its tray starts empty; give it a moment to come up.
	for (auto& entry : fItems) {
		entry.second->deskbarID = -1;
		entry.second->view = BMessenger();
	}
	g_timeout_add(750, &_ReAddTimeout, NULL);
}


/*!	Old-style XEmbed icons (Wine, older GTK 2 and Java apps) are what a
	System Tray Protocol manager embeds. KDE converts them with
	xembedsniproxy; any of these turns them into StatusNotifierItems that
	register with us. Needs X11 (XWayland under labwc is fine).
*/
void
Bridge::_StartXEmbedProxy()
{
	const char* setting = getenv("HOKKU_TRAY_XEMBED");
	if (setting != NULL && strcmp(setting, "0") == 0)
		return;
	if (getenv("DISPLAY") == NULL)
		return;

	const char* proxies[] = { "xembedsniproxy", "snixembed", NULL };
	for (const char** proxy = proxies; *proxy != NULL; proxy++) {
		gchar* path = g_find_program_in_path(*proxy);
		if (path == NULL)
			continue;

		gchar* argv[] = { path, NULL };
		GError* error = NULL;
		if (g_spawn_async(NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD, NULL,
				NULL, &fXEmbedProxy, &error)) {
			fprintf(stderr, "TrayBridge: XEmbed icons through %s\n", *proxy);
			g_free(path);
			return;
		}
		g_error_free(error);
		g_free(path);
	}
	fprintf(stderr, "TrayBridge: install xembedsniproxy or snixembed for "
		"XEmbed (legacy) tray icons\n");
}


// #pragma mark - TrayBridgeApp


static gpointer
run_main_loop(gpointer data)
{
	g_main_loop_run((GMainLoop*)data);
	return NULL;
}


static gboolean
start_bridge(gpointer data)
{
	sBridge->Start();
	return G_SOURCE_REMOVE;
}


static gboolean
handle_message(gpointer data)
{
	BMessage* message = (BMessage*)data;
	switch (message->what) {
		case kTBDeskbarStarted:
			sBridge->DeskbarStarted();
			break;
		default:
			sBridge->HandleMessage(message);
			break;
	}
	return G_SOURCE_REMOVE;
}


static void
delete_message(gpointer data)
{
	delete (BMessage*)data;
}


struct StopRequest {
	GMutex	lock;
	GCond	done;
	bool	finished;
};


static gboolean
stop_bridge(gpointer data)
{
	StopRequest* request = (StopRequest*)data;
	sBridge->Stop();
	g_mutex_lock(&request->lock);
	request->finished = true;
	g_cond_signal(&request->done);
	g_mutex_unlock(&request->lock);
	return G_SOURCE_REMOVE;
}


class TrayBridgeApp : public BApplication {
public:
	TrayBridgeApp()
		:
		BApplication(TRAY_BRIDGE_SIGNATURE),
		fLoop(NULL),
		fThread(NULL)
	{
	}

	virtual void ReadyToRun()
	{
		sBridge = new Bridge;
		fLoop = g_main_loop_new(NULL, FALSE);
		g_idle_add(&start_bridge, NULL);
		fThread = g_thread_new("traybridge dbus", &run_main_loop, fLoop);
	}

	virtual bool QuitRequested()
	{
		if (fLoop == NULL)
			return true;

		// take the replicants out before going
		StopRequest request;
		g_mutex_init(&request.lock);
		g_cond_init(&request.done);
		request.finished = false;
		g_main_context_invoke(NULL, &stop_bridge, &request);
		g_mutex_lock(&request.lock);
		gint64 deadline = g_get_monotonic_time() + 2 * G_TIME_SPAN_SECOND;
		while (!request.finished) {
			if (!g_cond_wait_until(&request.done, &request.lock, deadline))
				break;
		}
		g_mutex_unlock(&request.lock);

		g_main_loop_quit(fLoop);
		g_thread_join(fThread);
		g_main_loop_unref(fLoop);
		fLoop = NULL;
		return true;
	}

	virtual void MessageReceived(BMessage* message)
	{
		switch (message->what) {
			case kTBDeskbarStarted:
			case kTBRegisterView:
			case kTBActivate:
			case kTBScroll:
			case kTBMenuRequest:
			case kTBMenuEvent:
				_Forward(message);
				break;

			default:
				BApplication::MessageReceived(message);
				break;
		}
	}

private:
	void _Forward(BMessage* message)
	{
		g_main_context_invoke_full(NULL, G_PRIORITY_DEFAULT, &handle_message,
			new BMessage(*message), &delete_message);
	}

	GMainLoop*	fLoop;
	GThread*	fThread;
};


int
main()
{
	TrayBridgeApp app;
	app.Run();
	return 0;
}
