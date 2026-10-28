/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 *
 * org.freedesktop.Notifications for the notification_server: the desktop's
 * apps' notifications, shown the Haiku way. Everything D-Bus runs on a GLib
 * main loop thread of its own; notifications go to the server as
 * kNotificationMessage messages, as BNotification::Send() sends them.
 */


#include "DBusNotifications.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <algorithm>
#include <map>
#include <mutex>
#include <set>
#include <string>

#include <gio/gio.h>

#include <Application.h>
#include <Bitmap.h>
#include <Message.h>
#include <Messenger.h>
#include <Notification.h>
#include <Notifications.h>

#include "IconLoader.h"


namespace {

const char* kName = "org.freedesktop.Notifications";
const char* kPath = "/org/freedesktop/Notifications";

const char* kIntrospection =
	"<node>"
	"  <interface name='org.freedesktop.Notifications'>"
	"    <method name='GetCapabilities'>"
	"      <arg type='as' direction='out'/>"
	"    </method>"
	"    <method name='Notify'>"
	"      <arg name='app_name' type='s' direction='in'/>"
	"      <arg name='replaces_id' type='u' direction='in'/>"
	"      <arg name='app_icon' type='s' direction='in'/>"
	"      <arg name='summary' type='s' direction='in'/>"
	"      <arg name='body' type='s' direction='in'/>"
	"      <arg name='actions' type='as' direction='in'/>"
	"      <arg name='hints' type='a{sv}' direction='in'/>"
	"      <arg name='expire_timeout' type='i' direction='in'/>"
	"      <arg name='id' type='u' direction='out'/>"
	"    </method>"
	"    <method name='CloseNotification'>"
	"      <arg name='id' type='u' direction='in'/>"
	"    </method>"
	"    <method name='GetServerInformation'>"
	"      <arg name='name' type='s' direction='out'/>"
	"      <arg name='vendor' type='s' direction='out'/>"
	"      <arg name='version' type='s' direction='out'/>"
	"      <arg name='spec_version' type='s' direction='out'/>"
	"    </method>"
	"    <signal name='NotificationClosed'>"
	"      <arg name='id' type='u'/>"
	"      <arg name='reason' type='u'/>"
	"    </signal>"
	"    <signal name='ActionInvoked'>"
	"      <arg name='id' type='u'/>"
	"      <arg name='action_key' type='s'/>"
	"    </signal>"
	"  </interface>"
	"</node>";

// The icon is drawn at up to this size (see NotificationView).
const int kIconSize = 64;

GDBusConnection* sConnection = NULL;
guint32 sNextID = 1;

// the action keys each notification offered: "default" is a click on it,
// the others are its buttons
std::mutex sLock;
std::map<guint32, std::set<std::string> > sActions;


guint32
id_from_message_id(const char* messageID)
{
	size_t prefixLength = strlen(kDBusIDPrefix);
	if (messageID == NULL || strncmp(messageID, kDBusIDPrefix, prefixLength))
		return 0;
	return (guint32)strtoul(messageID + prefixLength, NULL, 10);
}


/*!	Apps send plain text, as "body-markup" isn't announced, but some send
	markup anyway: tags are dropped and entities decoded.
*/
std::string
strip_markup(const char* text)
{
	std::string result;
	bool inTag = false;
	for (const char* c = text; *c != '\0'; c++) {
		if (*c == '<') {
			if (strncasecmp(c, "<br", 3) == 0)
				result += '\n';
			inTag = true;
		} else if (*c == '>' && inTag)
			inTag = false;
		else if (!inTag && *c == '&') {
			static const struct { const char* entity; char character; }
				kEntities[] = { { "&amp;", '&' }, { "&lt;", '<' },
					{ "&gt;", '>' }, { "&quot;", '"' }, { "&apos;", '\'' },
					{ "&nbsp;", ' ' } };
			bool replaced = false;
			for (auto& entity : kEntities) {
				size_t length = strlen(entity.entity);
				if (strncmp(c, entity.entity, length) == 0) {
					result += entity.character;
					c += length - 1;
					replaced = true;
					break;
				}
			}
			if (!replaced)
				result += *c;
		} else if (!inTag)
			result += *c;
	}
	return result;
}


/*!	A signature for a Linux app, so the notification preferences can mute
	each one: "application/x-vnd.linux-" and its name.
*/
std::string
signature_for(const char* appName)
{
	std::string signature = "application/x-vnd.linux-";
	for (const char* c = appName; *c != '\0'; c++) {
		if (isalnum((unsigned char)*c) || *c == '.' || *c == '-')
			signature += (char)tolower((unsigned char)*c);
		else if (*c == ' ' || *c == '_')
			signature += '-';
	}
	return signature;
}


/*!	The image-data hint: (iiibiiay) width, height, row stride, alpha,
	bits per sample, channels, then the RGB(A) bytes.
*/
cairo_surface_t*
surface_from_image_data(GVariant* data)
{
	if (!g_variant_is_of_type(data, G_VARIANT_TYPE("(iiibiiay)")))
		return NULL;

	gint32 width, height, rowStride, bitsPerSample, channels;
	gboolean hasAlpha;
	GVariant* bytesVariant;
	g_variant_get(data, "(iiibii@ay)", &width, &height, &rowStride,
		&hasAlpha, &bitsPerSample, &channels, &bytesVariant);

	gsize length;
	const guchar* bytes = (const guchar*)g_variant_get_fixed_array(
		bytesVariant, &length, 1);
	cairo_surface_t* surface = NULL;
	if (width > 0 && height > 0 && bitsPerSample == 8
		&& (channels == 3 || channels == 4) && rowStride >= width * channels
		&& length >= (gsize)rowStride * (height - 1) + width * channels) {
		surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width,
			height);
		unsigned char* target = cairo_image_surface_get_data(surface);
		int targetStride = cairo_image_surface_get_stride(surface);
		for (int y = 0; y < height; y++) {
			const guchar* in = bytes + y * rowStride;
			uint32_t* out = (uint32_t*)(target + y * targetStride);
			for (int x = 0; x < width; x++, in += channels) {
				// cairo wants premultiplied native-endian ARGB
				uint32_t a = hasAlpha && channels == 4 ? in[3] : 255;
				uint32_t r = in[0] * a / 255;
				uint32_t g = in[1] * a / 255;
				uint32_t b = in[2] * a / 255;
				out[x] = (a << 24) | (r << 16) | (g << 8) | b;
			}
		}
		cairo_surface_mark_dirty(surface);
	}
	g_variant_unref(bytesVariant);
	return surface;
}


cairo_surface_t*
surface_from_name_or_path(const char* icon)
{
	if (icon == NULL || icon[0] == '\0')
		return NULL;

	if (strncmp(icon, "file://", 7) == 0) {
		gchar* path = g_filename_from_uri(icon, NULL, NULL);
		cairo_surface_t* surface = path != NULL
			? IconLoader::LoadFile(path, kIconSize) : NULL;
		g_free(path);
		return surface;
	}
	if (icon[0] == '/')
		return IconLoader::LoadFile(icon, kIconSize);
	return IconLoader::LoadNamed(icon, kIconSize);
}


/*!	In the order the specification gives: the image-data hint (or its
	older names), image-path, then the app's icon.
*/
BBitmap*
icon_for(const char* appIcon, GVariant* hints)
{
	cairo_surface_t* surface = NULL;
	const char* dataHints[] = { "image-data", "image_data", "icon_data" };
	for (const char* hint : dataHints) {
		GVariant* data = g_variant_lookup_value(hints, hint, NULL);
		if (data != NULL) {
			surface = surface_from_image_data(data);
			g_variant_unref(data);
		}
		if (surface != NULL)
			break;
	}
	if (surface == NULL) {
		const char* path = NULL;
		if (g_variant_lookup(hints, "image-path", "&s", &path)
			|| g_variant_lookup(hints, "image_path", "&s", &path)) {
			surface = surface_from_name_or_path(path);
		}
	}
	if (surface == NULL)
		surface = surface_from_name_or_path(appIcon);
	if (surface == NULL)
		return NULL;

	BBitmap* bitmap = IconLoader::MakeBitmap(surface, kIconSize);
	cairo_surface_destroy(surface);
	return bitmap;
}


guint32
notify(GVariant* parameters)
{
	const char* appName;
	guint32 replacesID;
	const char* appIcon;
	const char* summary;
	const char* body;
	GVariant* actions;
	GVariant* hints;
	gint32 expireTimeout;
	g_variant_get(parameters, "(&su&s&s&s@as@a{sv}i)", &appName, &replacesID,
		&appIcon, &summary, &body, &actions, &hints, &expireTimeout);

	guint32 id = replacesID != 0 ? replacesID : sNextID++;

	// urgency 2 is critical; a "value" hint (0 to 100) is a progress bar
	notification_type type = B_INFORMATION_NOTIFICATION;
	guchar urgency = 1;
	gint32 progress = -1;
	g_variant_lookup(hints, "urgency", "y", &urgency);
	g_variant_lookup(hints, "value", "i", &progress);
	if (progress >= 0)
		type = B_PROGRESS_NOTIFICATION;
	else if (urgency >= 2)
		type = B_IMPORTANT_NOTIFICATION;

	BNotification notification(type);
	notification.SetGroup(appName[0] != '\0' ? appName : "Notification");
	notification.SetTitle(strip_markup(summary).c_str());
	notification.SetContent(strip_markup(body).c_str());
	std::string messageID = kDBusIDPrefix + std::to_string(id);
	notification.SetMessageID(messageID.c_str());
	if (progress >= 0)
		notification.SetProgress(std::min(progress, 100) / 100.0f);

	BBitmap* icon = icon_for(appIcon, hints);
	if (icon != NULL) {
		notification.SetIcon(icon);
		delete icon;
	}

	// key, label, key, label...: "default" is done by a click, the others
	// are the notification's actions (buttons), each telling the server
	// which key to report
	std::set<std::string> keys;
	gsize count = g_variant_n_children(actions);
	for (gsize i = 0; i + 1 < count; i += 2) {
		const char* key;
		const char* label;
		g_variant_get_child(actions, i, "&s", &key);
		g_variant_get_child(actions, i + 1, "&s", &label);
		keys.insert(key);
		if (strcmp(key, "default") != 0) {
			BMessage* invoked = new BMessage(kDBusActionInvoked);
			invoked->AddString("id", messageID.c_str());
			invoked->AddString("key", key);
			notification.AddAction(label, invoked, be_app_messenger);
		}
	}
	{
		std::lock_guard<std::mutex> locker(sLock);
		if (keys.empty())
			sActions.erase(id);
		else
			sActions[id] = keys;
	}

	BMessage message(kNotificationMessage);
	if (notification.Archive(&message) == B_OK) {
		// the Linux app it comes from, not the server
		message.ReplaceString("_appname", appName);
		message.ReplaceString("_signature", signature_for(appName).c_str());
		if (expireTimeout == 0)
			message.AddInt64("timeout", 0);
				// never expires
		else if (expireTimeout > 0)
			message.AddInt64("timeout", (bigtime_t)expireTimeout * 1000);
		be_app_messenger.SendMessage(&message);
	}

	g_variant_unref(actions);
	g_variant_unref(hints);
	return id;
}


void
method_call(GDBusConnection* connection, const gchar* sender,
	const gchar* path, const gchar* interface, const gchar* method,
	GVariant* parameters, GDBusMethodInvocation* invocation, gpointer data)
{
	if (strcmp(method, "Notify") == 0) {
		guint32 id = notify(parameters);
		g_dbus_method_invocation_return_value(invocation,
			g_variant_new("(u)", id));
	} else if (strcmp(method, "CloseNotification") == 0) {
		guint32 id;
		g_variant_get(parameters, "(u)", &id);
		BMessage message(kCloseNotification);
		message.AddString("id", (kDBusIDPrefix + std::to_string(id)).c_str());
		be_app_messenger.SendMessage(&message);
		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (strcmp(method, "GetCapabilities") == 0) {
		const gchar* capabilities[] = { "body", "actions", "icon-static",
			NULL };
		g_dbus_method_invocation_return_value(invocation,
			g_variant_new("(^as)", capabilities));
	} else if (strcmp(method, "GetServerInformation") == 0) {
		g_dbus_method_invocation_return_value(invocation,
			g_variant_new("(ssss)", "notification_server", "Hokku", "1.0",
				"1.2"));
	} else {
		g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
			G_DBUS_ERROR_UNKNOWN_METHOD, "Unknown method %s", method);
	}
}


void
name_acquired(GDBusConnection* connection, const gchar* name, gpointer data)
{
	fprintf(stderr, "notification_server: showing the desktop's "
		"notifications (%s)\n", name);
}


void
name_lost(GDBusConnection* connection, const gchar* name, gpointer data)
{
	// Another server (Plasma's, GNOME's) has it, or we lost it to one.
	// We stay in the queue and take over if it goes.
	fprintf(stderr, "notification_server: another notification server "
		"shows the desktop's notifications\n");
}


gpointer
run(gpointer data)
{
	GMainContext* context = g_main_context_new();
	g_main_context_push_thread_default(context);

	GError* error = NULL;
	GDBusConnection* connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL,
		&error);
	if (connection == NULL) {
		fprintf(stderr, "notification_server: no session bus: %s\n",
			error->message);
		g_error_free(error);
		return NULL;
	}

	GDBusNodeInfo* info = g_dbus_node_info_new_for_xml(kIntrospection, NULL);
	static const GDBusInterfaceVTable vtable = { &method_call, NULL, NULL,
		{ 0 } };
	g_dbus_connection_register_object(connection, kPath, info->interfaces[0],
		&vtable, NULL, NULL, NULL);
	g_dbus_node_info_unref(info);
	g_atomic_pointer_set(&sConnection, connection);

	g_bus_own_name_on_connection(connection, kName,
		G_BUS_NAME_OWNER_FLAGS_NONE, &name_acquired, &name_lost, NULL, NULL);

	GMainLoop* loop = g_main_loop_new(context, FALSE);
	g_main_loop_run(loop);
	return NULL;
}

}	// namespace


namespace DBusNotifications {

void
Start()
{
	g_thread_unref(g_thread_new("notification_server dbus", &run, NULL));
}


void
ActionInvoked(const char* messageID, const char* key)
{
	guint32 id = id_from_message_id(messageID);
	GDBusConnection* connection
		= (GDBusConnection*)g_atomic_pointer_get(&sConnection);
	if (id == 0 || connection == NULL)
		return;

	{
		// only actions the app offered
		std::lock_guard<std::mutex> locker(sLock);
		auto found = sActions.find(id);
		if (found == sActions.end()
			|| found->second.find(key) == found->second.end())
			return;
	}
	// GDBus connections may be used from any thread
	g_dbus_connection_emit_signal(connection, NULL, kPath, kName,
		"ActionInvoked", g_variant_new("(us)", id, key), NULL);
}


void
NotificationClosed(const char* messageID, int32 reason)
{
	guint32 id = id_from_message_id(messageID);
	GDBusConnection* connection
		= (GDBusConnection*)g_atomic_pointer_get(&sConnection);
	if (id == 0 || connection == NULL)
		return;

	{
		std::lock_guard<std::mutex> locker(sLock);
		sActions.erase(id);
	}
	g_dbus_connection_emit_signal(connection, NULL, kPath, kName,
		"NotificationClosed", g_variant_new("(uu)", id, (guint32)reason),
		NULL);
}

}	// namespace DBusNotifications
