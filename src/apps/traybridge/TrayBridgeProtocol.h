/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Messages exchanged between TrayBridge (the StatusNotifierItem host that
 * talks to D-Bus) and the TrayBridgeItem replicants it puts into the
 * Deskbar tray. Shared by src/apps/traybridge and src/apps/deskbar.
 */
#ifndef TRAY_BRIDGE_PROTOCOL_H
#define TRAY_BRIDGE_PROTOCOL_H

#include <SupportDefs.h>


#define TRAY_BRIDGE_SIGNATURE	"application/x-vnd.Hokku-TrayBridge"
#define TRAY_BRIDGE_ITEM_CLASS	"TrayBridgeItem"
#define TRAY_BRIDGE_VIEW_PREFIX	"traybridge:"
	// replicant view names are TRAY_BRIDGE_VIEW_PREFIX + item key

// Fields stored in a replicant archive.
#define TB_FIELD_ITEM		"tb:item"		// string, item key (service + path)
#define TB_FIELD_BRIDGE		"tb:bridge"		// BMessenger of the bridge app
#define TB_FIELD_ICON		"tb:icon"		// archived BBitmap (B_RGBA32)
#define TB_FIELD_TOOLTIP	"tb:tooltip"	// string


enum {
	// replicant -> bridge
	kTBRegisterView		= 'tbRg',
		// "tb:item" string, "view" BMessenger, "size" int32 (icon edge)
	kTBActivate			= 'tbAc',
		// "tb:item", "x"/"y" int32 (screen), "button" int32 (1 or 2)
	kTBScroll			= 'tbSc',
		// "tb:item", "delta" int32, "orientation" string
	kTBMenuRequest		= 'tbMr',
		// "tb:item", "x"/"y" int32 (screen), "primary" bool
	kTBMenuEvent		= 'tbMe',
		// "tb:item", "id" int32 (dbusmenu item id), no "id" when the menu
		// closed without a choice

	// Deskbar -> bridge
	kTBDeskbarStarted	= 'tbDs',
		// a new Deskbar found the bridge running: its tray is empty

	// bridge -> replicant
	kTBUpdate			= 'tbUp',
		// optional "tb:icon" archived BBitmap, optional "tb:tooltip" string
	kTBShowMenu			= 'tbSm',
		// "menu" BMessage tree (see below), "where" BPoint (screen)
};


/*	A menu tree as sent with kTBShowMenu. Each node is a BMessage with:
		"id"			int32	dbusmenu item id
		"label"			string	label, mnemonics already stripped
		"separator"		bool
		"enabled"		bool
		"toggle"		int32	0 none, 1 checkmark, 2 radio
		"checked"		bool
		"icon"			BMessage, optional archived BBitmap
		"child"			BMessage, repeated, submenu entries
*/

#endif	// TRAY_BRIDGE_PROTOCOL_H
