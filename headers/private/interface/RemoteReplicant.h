/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef _REMOTE_REPLICANT_H
#define _REMOTE_REPLICANT_H


#include <SupportDefs.h>


class BMessage;
class BPopUpMenu;


/*!	Remote replicants: a replicant whose code its host can't load.

	On Haiku, Deskbar runs an app's replicant itself, loading the app's
	executable as an add-on. Linux won't load an executable into another
	process, so when a host (Deskbar) can't find a replicant's class, it asks
	the app named by the archive's "add_on" signature to run the replicant
	instead. The app instantiates the view in a window of its own that is
	never on screen, copies what it draws into a shared area after every
	update, and the host shows those pixels and passes the pointer back. The
	replicant's messaging works as written, since it runs in its own app.

	Host to app (by signature):
		kMsgRemoteReplicantStart	"archive" (message), "host" (messenger),
									"screen_origin" (point), "export_menus"
									(bool: see below)
	App to host:
		kMsgRemoteReplicantStarted	"replicant" (messenger to the window),
									"area" (int32), "width", "height" (int32)
		kMsgRemoteReplicantFailed	"error" (int32)
		kMsgRemoteReplicantPixels	the area holds new pixels
		kMsgRemoteReplicantMenu		"menu" (message): the popup menu the
									replicant opened, for the host to show
	Host to replicant:
		kMsgRemoteReplicantEvent	"event" (a B_MOUSE_* message in the
									replicant's coordinates), "screen_origin"
		kMsgRemoteReplicantMoved	"screen_origin"
		kMsgRemoteReplicantStop		the host let it go
		kMsgRemoteReplicantMenuChoice	"path" (int32s: item indices from the
									top menu down): the user chose that item

	Menus: an app's popup menu can't be shown over another app's window on
	Wayland, so with "export_menus" the replicant's BPopUpMenu::Go() sends
	the menu to the host instead, which shows it itself (Deskbar as a menu
	of its own, TrayExport to the tray as a dbusmenu). A menu message has an
	"item" message per item: "label" (string), "enabled", "marked",
	"separator" (bools), and "submenu" (a menu message) when it has one.
*/


namespace BPrivate {


enum {
	kMsgRemoteReplicantStart	= '_rrS',
	kMsgRemoteReplicantStarted	= '_rrO',
	kMsgRemoteReplicantFailed	= '_rrF',
	kMsgRemoteReplicantPixels	= '_rrP',
	kMsgRemoteReplicantEvent	= '_rrE',
	kMsgRemoteReplicantMoved	= '_rrM',
	kMsgRemoteReplicantStop		= '_rrQ',
	kMsgRemoteReplicantMenu		= '_rrN',
	kMsgRemoteReplicantMenuChoice = '_rrC'
};


// The area starts with this header; B_RGBA32 pixels (not premultiplied)
// follow at kRemoteReplicantPixelOffset.
struct remote_replicant_header {
	uint32	magic;
	int32	width;
	int32	height;
	int32	bytes_per_row;
	int32	sequence;		// incremented after each copy
};

static const uint32 kRemoteReplicantMagic = 'RRpx';
static const size_t kRemoteReplicantPixelOffset = 64;


// App side: runs the replicant a host asked for. Called by BApplication.
void start_remote_replicant(BMessage* request);

// App side: if the calling thread is a remote replicant's whose host shows
// its menus, sends menu to the host and returns true (BPopUpMenu::Go()
// then returns without showing it).
bool export_remote_replicant_menu(BPopUpMenu* menu);


}	// namespace BPrivate


#endif	// _REMOTE_REPLICANT_H
