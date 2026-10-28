/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 */


#include "TrayBridgeItem.h"

#include <math.h>

#include <algorithm>

#include <Bitmap.h>
#include <Menu.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <SeparatorItem.h>
#include <Window.h>

#include "TrayBridgeProtocol.h"


namespace {

/*!	A menu item that draws an icon in front of its label, for dbusmenu
	entries that have "icon-name" or "icon-data".
*/
class IconMenuItem : public BMenuItem {
public:
	IconMenuItem(const char* label, BMessage* message, BBitmap* icon)
		:
		BMenuItem(label, message),
		fIcon(icon)
	{
	}

	IconMenuItem(BMenu* submenu, BMessage* message, BBitmap* icon)
		:
		BMenuItem(submenu, message),
		fIcon(icon)
	{
	}

	virtual ~IconMenuItem()
	{
		delete fIcon;
	}

	virtual void GetContentSize(float* width, float* height)
	{
		BMenuItem::GetContentSize(width, height);
		if (fIcon != NULL) {
			*width += fIcon->Bounds().Width() + 1 + _Spacing();
			*height = std::max(*height, fIcon->Bounds().Height() + 1);
		}
	}

	virtual void DrawContent()
	{
		if (fIcon != NULL) {
			BMenu* menu = Menu();
			BPoint location = ContentLocation();
			BRect frame = Frame();
			float iconHeight = fIcon->Bounds().Height() + 1;
			BPoint iconLocation(location.x,
				frame.top + floorf((frame.Height() + 1 - iconHeight) / 2));

			menu->PushState();
			menu->SetDrawingMode(B_OP_ALPHA);
			menu->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
			if (!IsEnabled())
				menu->SetHighColor(0, 0, 0, 96);
			menu->DrawBitmap(fIcon, iconLocation);
			menu->PopState();

			menu->MovePenTo(location.x + fIcon->Bounds().Width() + 1
				+ _Spacing(), menu->PenLocation().y);
		}
		BMenuItem::DrawContent();
	}

private:
	float _Spacing() const
	{
		return 6;
	}

	BBitmap* fIcon;
};


BBitmap*
unarchive_bitmap(BMessage* archive)
{
	if (archive == NULL || archive->IsEmpty())
		return NULL;

	BArchivable* object = BBitmap::Instantiate(archive);
	BBitmap* bitmap = dynamic_cast<BBitmap*>(object);
	if (bitmap == NULL) {
		delete object;
		return NULL;
	}
	if (bitmap->InitCheck() != B_OK) {
		delete bitmap;
		return NULL;
	}
	return bitmap;
}

const uint32 kMsgMenuClosed = 'tbmc';


/*!	Tells the replicant when it goes away, which an asynchronous BPopUpMenu
	does when it closes, after invoking the chosen item if there was one.
*/
class TrayPopUpMenu : public BPopUpMenu {
public:
	TrayPopUpMenu(const BMessenger& replicant)
		:
		BPopUpMenu("tray item menu", false, false),
		fReplicant(replicant)
	{
	}

	virtual ~TrayPopUpMenu()
	{
		fReplicant.SendMessage(kMsgMenuClosed);
	}

private:
	BMessenger	fReplicant;
};

}	// namespace


TrayBridgeItem::TrayBridgeItem(BMessage* archive)
	:
	BView(archive),
	fIcon(NULL),
	fMenuChoiceSent(false)
{
	archive->FindString(TB_FIELD_ITEM, &fItem);
	archive->FindMessenger(TB_FIELD_BRIDGE, &fBridge);

	BMessage icon;
	if (archive->FindMessage(TB_FIELD_ICON, &icon) == B_OK)
		_SetIcon(&icon);

	const char* toolTip;
	if (archive->FindString(TB_FIELD_TOOLTIP, &toolTip) == B_OK
		&& toolTip[0] != '\0') {
		SetToolTip(toolTip);
	}
}


TrayBridgeItem::~TrayBridgeItem()
{
	delete fIcon;
}


BArchivable*
TrayBridgeItem::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, TRAY_BRIDGE_ITEM_CLASS))
		return NULL;

	return new TrayBridgeItem(archive);
}


status_t
TrayBridgeItem::Archive(BMessage* into, bool deep) const
{
	status_t status = BView::Archive(into, deep);
	if (status != B_OK)
		return status;

	into->AddString(TB_FIELD_ITEM, fItem);
	into->AddMessenger(TB_FIELD_BRIDGE, fBridge);
	into->AddString("add_on", TRAY_BRIDGE_SIGNATURE);
	if (fIcon != NULL) {
		BMessage icon;
		if (fIcon->Archive(&icon) == B_OK)
			into->AddMessage(TB_FIELD_ICON, &icon);
	}
	return B_OK;
}


void
TrayBridgeItem::AttachedToWindow()
{
	BView::AttachedToWindow();
	AdoptParentColors();
	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);

	// Tell the bridge where this replicant lives now, so it can push icon
	// and tooltip changes straight to it.
	_Register();
}


void
TrayBridgeItem::Draw(BRect updateRect)
{
	if (fIcon == NULL)
		return;

	BRect bounds = Bounds();
	BRect iconBounds = fIcon->Bounds();
	BPoint where(
		floorf(bounds.left + (bounds.Width() - iconBounds.Width()) / 2),
		floorf(bounds.top + (bounds.Height() - iconBounds.Height()) / 2));
	DrawBitmap(fIcon, where);
}


void
TrayBridgeItem::MouseDown(BPoint where)
{
	int32 buttons = B_PRIMARY_MOUSE_BUTTON;
	if (Window() != NULL && Window()->CurrentMessage() != NULL)
		Window()->CurrentMessage()->FindInt32("buttons", &buttons);

	BPoint screen = ConvertToScreen(where);

	if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0) {
		BMessage request(kTBMenuRequest);
		request.AddString(TB_FIELD_ITEM, fItem);
		request.AddInt32("x", (int32)screen.x);
		request.AddInt32("y", (int32)screen.y);
		request.AddBool("primary", false);
		_SendToBridge(&request);
		return;
	}

	BMessage activate(kTBActivate);
	activate.AddString(TB_FIELD_ITEM, fItem);
	activate.AddInt32("x", (int32)screen.x);
	activate.AddInt32("y", (int32)screen.y);
	activate.AddInt32("button",
		(buttons & B_TERTIARY_MOUSE_BUTTON) != 0 ? 2 : 1);
	_SendToBridge(&activate);
}


void
TrayBridgeItem::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kTBUpdate:
		{
			BMessage icon;
			if (message->FindMessage(TB_FIELD_ICON, &icon) == B_OK) {
				_SetIcon(&icon);
				Invalidate();
			}
			const char* toolTip;
			if (message->FindString(TB_FIELD_TOOLTIP, &toolTip) == B_OK)
				SetToolTip(toolTip[0] != '\0' ? toolTip : NULL);
			break;
		}

		case kTBShowMenu:
			_ShowMenu(message);
			break;

		case kTBMenuEvent:
		{
			// a menu item was chosen
			BMessage event(kTBMenuEvent);
			event.AddString(TB_FIELD_ITEM, fItem);
			event.AddInt32("id", message->GetInt32("id", 0));
			_SendToBridge(&event);
			fMenuChoiceSent = true;
			break;
		}

		case kMsgMenuClosed:
			if (!fMenuChoiceSent) {
				// closed without a choice: dbusmenu apps want to know
				BMessage event(kTBMenuEvent);
				event.AddString(TB_FIELD_ITEM, fItem);
				_SendToBridge(&event);
			}
			fMenuChoiceSent = false;
			break;

		case B_MOUSE_WHEEL_CHANGED:
		{
			float deltaX = 0, deltaY = 0;
			message->FindFloat("be:wheel_delta_x", &deltaX);
			message->FindFloat("be:wheel_delta_y", &deltaY);

			// StatusNotifierItem follows Qt: 120 per notch, positive is
			// up (or right). Haiku's wheel is positive down.
			BMessage scroll(kTBScroll);
			scroll.AddString(TB_FIELD_ITEM, fItem);
			if (deltaY != 0) {
				scroll.AddInt32("delta", (int32)(-deltaY * 120));
				scroll.AddString("orientation", "vertical");
			} else if (deltaX != 0) {
				scroll.AddInt32("delta", (int32)(deltaX * 120));
				scroll.AddString("orientation", "horizontal");
			} else
				break;
			_SendToBridge(&scroll);
			break;
		}

		default:
			BView::MessageReceived(message);
			break;
	}
}


void
TrayBridgeItem::_SetIcon(BMessage* archivedBitmap)
{
	BBitmap* icon = unarchive_bitmap(archivedBitmap);
	if (icon == NULL)
		return;

	delete fIcon;
	fIcon = icon;
}


void
TrayBridgeItem::_Register()
{
	BMessage registration(kTBRegisterView);
	registration.AddString(TB_FIELD_ITEM, fItem);
	registration.AddMessenger("view", BMessenger(this));
	registration.AddInt32("size",
		(int32)std::min(Bounds().Width(), Bounds().Height()) + 1);
	_SendToBridge(&registration);
}


void
TrayBridgeItem::_ShowMenu(BMessage* message)
{
	BMessage root;
	if (message->FindMessage("menu", &root) != B_OK)
		return;

	BPoint where;
	if (message->FindPoint("where", &where) != B_OK)
		where = ConvertToScreen(Bounds().LeftBottom());

	BPopUpMenu* menu = new TrayPopUpMenu(BMessenger(this));
	menu->SetFont(be_plain_font);
	_AddMenuItems(menu, &root);
	if (menu->CountItems() == 0) {
		delete menu;
		return;
	}

	fMenuChoiceSent = false;
	menu->SetAsyncAutoDestruct(true);
	BRect clickToOpen = ConvertToScreen(Bounds());
	menu->Go(where, true, true, clickToOpen, true);
}


void
TrayBridgeItem::_AddMenuItems(BMenu* menu, BMessage* node)
{
	BMessage child;
	for (int32 i = 0; node->FindMessage("child", i, &child) == B_OK; i++) {
		if (child.GetBool("separator", false)) {
			// no leading, trailing or doubled separators
			int32 count = menu->CountItems();
			if (count > 0 && dynamic_cast<BSeparatorItem*>(
					menu->ItemAt(count - 1)) == NULL) {
				menu->AddItem(new BSeparatorItem());
			}
			continue;
		}

		const char* label = child.GetString("label", "");
		int32 id = child.GetInt32("id", 0);

		BMessage iconArchive;
		BBitmap* icon = NULL;
		if (child.FindMessage("icon", &iconArchive) == B_OK)
			icon = unarchive_bitmap(&iconArchive);

		BMenuItem* item;
		if (child.HasMessage("child")) {
			BMenu* submenu = new BMenu(label);
			submenu->SetFont(be_plain_font);
			_AddMenuItems(submenu, &child);
			item = icon != NULL
				? new IconMenuItem(submenu, NULL, icon)
				: new BMenuItem(submenu);
		} else {
			// to this replicant, which passes it on to the bridge
			BMessage* invoke = new BMessage(kTBMenuEvent);
			invoke->AddInt32("id", id);
			item = icon != NULL
				? new IconMenuItem(label, invoke, icon)
				: new BMenuItem(label, invoke);
			item->SetTarget(this);
		}

		item->SetEnabled(child.GetBool("enabled", true));
		if (child.GetInt32("toggle", 0) != 0)
			item->SetMarked(child.GetBool("checked", false));

		menu->AddItem(item);
	}

	// drop a trailing separator
	int32 count = menu->CountItems();
	if (count > 0
		&& dynamic_cast<BSeparatorItem*>(menu->ItemAt(count - 1)) != NULL) {
		delete menu->RemoveItem(count - 1);
	}
}


void
TrayBridgeItem::_SendToBridge(BMessage* message)
{
	if (!fBridge.IsValid()) {
		// The bridge was restarted; the new one re-adds its items, so this
		// one is stale and goes away soon.
		fBridge = BMessenger(TRAY_BRIDGE_SIGNATURE);
	}
	fBridge.SendMessage(message);
}
