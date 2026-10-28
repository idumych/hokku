/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef TRAY_BRIDGE_ITEM_H
#define TRAY_BRIDGE_ITEM_H


#include <Messenger.h>
#include <String.h>
#include <View.h>


class BBitmap;
class BMenu;


/*!	A tray replicant standing in for one Linux tray icon (a
	StatusNotifierItem, or an XEmbed icon proxied to one). TrayBridge adds it
	with BDeskbar::AddItem(); the class lives in Deskbar itself because
	Cosmoe instantiates replicants only from images already loaded.
*/
class TrayBridgeItem : public BView {
public:
								TrayBridgeItem(BMessage* archive);
	virtual						~TrayBridgeItem();

	static	BArchivable*		Instantiate(BMessage* archive);
	virtual	status_t			Archive(BMessage* into, bool deep = true) const;

	virtual	void				AttachedToWindow();
	virtual	void				Draw(BRect updateRect);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MessageReceived(BMessage* message);

private:
			void				_SetIcon(BMessage* archivedBitmap);
			void				_Register();
			void				_ShowMenu(BMessage* message);
			void				_AddMenuItems(BMenu* menu, BMessage* node);
			void				_SendToBridge(BMessage* message);

			BString				fItem;
			BMessenger			fBridge;
			BBitmap*			fIcon;
};


#endif	// TRAY_BRIDGE_ITEM_H
