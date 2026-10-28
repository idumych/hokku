/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef DBUS_MENU_H
#define DBUS_MENU_H


#include <string>

#include <gio/gio.h>

#include <Messenger.h>


namespace DBusMenu {

/*!	Fetches the com.canonical.dbusmenu tree at service/path in a thread of
	its own (the calls are synchronous, and some apps fill their menus only
	when asked) and sends it to view as a kTBShowMenu message.
*/
void	ShowAsync(GDBusConnection* connection, const std::string& service,
			const std::string& path, const std::string& iconThemePath,
			int x, int y, const BMessenger& view);

// Tells the app that menu item id was chosen.
void	SendClicked(GDBusConnection* connection, const std::string& service,
			const std::string& path, int32 id);

}	// namespace DBusMenu


#endif	// DBUS_MENU_H
