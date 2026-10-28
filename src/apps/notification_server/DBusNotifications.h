/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef DBUS_NOTIFICATIONS_H
#define DBUS_NOTIFICATIONS_H


#include <SupportDefs.h>


/*!	Hokku: the notification_server also shows the desktop's notifications.
	It serves org.freedesktop.Notifications on the session bus when no other
	notification server does (a desktop of Hokku apps; under Plasma or GNOME
	their own shells keep it), and shows each one as a BNotification. These
	have the message ID kDBusIDPrefix + their D-Bus ID, so a replacement
	updates them in place and the views know to report back.
*/

#define kDBusIDPrefix "freedesktop-"

// In a kNotificationMessage, the actions other than "default", shown as
// buttons: their keys and labels, in order
#define kActionKeyField		"_hokku:action"
#define kActionLabelField	"_hokku:action_label"

enum {
	// view or window -> server
	kDBusActionInvoked		= 'nsDa',
		// "id" string (message ID), "key" string (none: "default"): the
		// notification was clicked, or one of its action buttons
	kDBusNotificationClosed	= 'nsDc',
		// "id" string (message ID), "reason" int32 (see below)

	// server -> window
	kCloseNotification		= 'nsDx',
		// "id" string (message ID): CloseNotification() was called
};

// NotificationClosed reasons, as in the specification
enum {
	kClosedExpired = 1,
	kClosedDismissed = 2,
	kClosedByCall = 3,
};


namespace DBusNotifications {

// Starts serving on a thread of its own; the server's notification window
// gets the notifications as kNotificationMessage messages.
void	Start();

// Report to the app that sent notification messageID (a no-op for others).
void	ActionInvoked(const char* messageID, const char* key);
void	NotificationClosed(const char* messageID, int32 reason);

}	// namespace DBusNotifications


#endif	// DBUS_NOTIFICATIONS_H
