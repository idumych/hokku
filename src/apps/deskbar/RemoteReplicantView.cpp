/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 *
 * The host side of remote replicants (see RemoteReplicant.h).
 */


#include "RemoteReplicantView.h"

#include <Bitmap.h>
#include <MenuItem.h>
#include <Path.h>
#include <MessageRunner.h>
#include <PopUpMenu.h>
#include <SeparatorItem.h>
#include <Roster.h>
#include <Window.h>

#include <stdio.h>
#include <stdlib.h>
#include <spawn.h>
#include <string.h>
#include <unistd.h>

#include <DeskbarPrivate.h>
#include <RemoteReplicant.h>


using namespace BPrivate;


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
		fprintf(stderr, "Deskbar remote replicant: "); \
		fprintf(stderr, __VA_ARGS__); \
	} \
} while (0)

static const uint32 kMsgCheck = 'rrCk';
static const uint32 kMsgMenuChoice = 'rrCh';
static const uint32 kMsgStartApp = 'rrSa';
static const uint32 kMsgRemoveSelf = 'rrRm';

extern char** environ;
static const char* kArchiveField = "remote:archive";
static const char* kSignatureField = "remote:signature";
// How often to look for the app again while it isn't running, and to
// notice that it quit.
static const bigtime_t kCheckInterval = 1000000;


RemoteReplicantView::RemoteReplicantView(BMessage* archive)
	:
	BView(archive->FindRect("_frame"), archive->FindString("_name"),
		B_FOLLOW_NONE, B_WILL_DRAW | B_FRAME_EVENTS),
	fCheckRunner(NULL),
	fArea(-1),
	fAreaBase(NULL),
	fBitmap(NULL),
	fStarting(false),
	fStartTime(0),
	fButtons(0)
{
	archive->FindMessage(kArchiveField, &fReplicantArchive);
	fSignature = archive->FindString(kSignatureField);
	SetViewColor(B_TRANSPARENT_COLOR);
}


RemoteReplicantView::~RemoteReplicantView()
{
	_Disconnect();
	delete fBitmap;
}


/*static*/ status_t
RemoteReplicantView::Wrap(const BMessage* replicantArchive, BMessage* into)
{
	const char* signature;
	if (replicantArchive->FindString("add_on", &signature) != B_OK)
		return B_BAD_VALUE;

	into->MakeEmpty();
	into->what = replicantArchive->what;
	into->AddString("class", "RemoteReplicantView");
	into->AddString("_name", replicantArchive->GetString("_name", ""));
	into->AddRect("_frame", replicantArchive->GetRect("_frame",
		BRect(0, 0, 15, 15)));
	into->AddString(kSignatureField, signature);
	into->AddMessage(kArchiveField, replicantArchive);
	return B_OK;
}


/*static*/ BArchivable*
RemoteReplicantView::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, "RemoteReplicantView"))
		return NULL;
	return new RemoteReplicantView(archive);
}


status_t
RemoteReplicantView::Archive(BMessage* into, bool deep) const
{
	return Wrap(&fReplicantArchive, into);
}


void
RemoteReplicantView::AttachedToWindow()
{
	BView::AttachedToWindow();
	TRACE("\"%s\" attached, %gx%g at %g,%g in %s\n", Name(),
		Bounds().Width() + 1, Bounds().Height() + 1, Frame().left, Frame().top,
		Parent() != NULL ? Parent()->Name() : "?");
	BMessage check(kMsgCheck);
	fCheckRunner = new BMessageRunner(BMessenger(this), &check,
		kCheckInterval);
	_Start();
}


void
RemoteReplicantView::DetachedFromWindow()
{
	delete fCheckRunner;
	fCheckRunner = NULL;
	if (fReplicant.IsValid())
		fReplicant.SendMessage(kMsgRemoteReplicantStop);
	_Disconnect();
	BView::DetachedFromWindow();
}


void
RemoteReplicantView::Draw(BRect updateRect)
{
	// The tray's own background, then whatever the replicant drew; the
	// last picture stays while the app isn't running.
	BView* parent = Parent();
	SetHighColor(parent != NULL ? parent->ViewColor()
		: ui_color(B_PANEL_BACKGROUND_COLOR));
	FillRect(updateRect);
	if (fBitmap != NULL) {
		SetDrawingMode(B_OP_ALPHA);
		SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		DrawBitmap(fBitmap, B_ORIGIN);
		SetDrawingMode(B_OP_COPY);
	}
}


void
RemoteReplicantView::FrameMoved(BPoint newPosition)
{
	if (fReplicant.IsValid()) {
		BMessage moved(kMsgRemoteReplicantMoved);
		moved.AddPoint("screen_origin", ConvertToScreen(B_ORIGIN));
		fReplicant.SendMessage(&moved);
	}
}


void
RemoteReplicantView::MouseDown(BPoint where)
{
	TRACE("mouse down at %g,%g (%s)\n", where.x, where.y,
		fReplicant.IsValid() ? "forwarding" : "app not running");
	if (!fReplicant.IsValid()) {
		// Nothing runs the replicant: our own menu, to start the app or
		// take the item out.
		_ShowHostMenu();
		return;
	}
	if (Window()->CurrentMessage() != NULL)
		fButtons = Window()->CurrentMessage()->GetInt32("buttons", 0);
	SetMouseEventMask(B_POINTER_EVENTS, B_LOCK_WINDOW_FOCUS);
	_Forward(where);
}


void
RemoteReplicantView::MouseUp(BPoint where)
{
	fButtons = 0;
	_Forward(where);
}


void
RemoteReplicantView::MouseMoved(BPoint where, uint32 transit,
	const BMessage* dragMessage)
{
	_Forward(where);
}


void
RemoteReplicantView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgRemoteReplicantStarted:
			_Started(message);
			break;

		case kMsgRemoteReplicantFailed:
			fprintf(stderr, "Deskbar: %s can't run its replicant \"%s\": %s\n",
				fSignature.String(), Name(),
				strerror(message->GetInt32("error", B_ERROR)));
			fStarting = false;
			break;

		case kMsgRemoteReplicantPixels:
			_CopyPixels();
			break;

		case kMsgRemoteReplicantMenu:
			_ShowMenu(message);
			break;

		case kMsgStartApp:
			_Launch();
			break;

		case kMsgRemoveSelf:
		{
			// Through Deskbar's own request, which deletes us.
			BMessage remove(kMsgRemoveItem);
			remove.AddString("name", Name());
			Window()->PostMessage(&remove);
			break;
		}

		case kMsgMenuChoice:
			if (fReplicant.IsValid()) {
				BMessage choice(kMsgRemoteReplicantMenuChoice);
				int32 index;
				for (int32 i = 0;
						message->FindInt32("path", i, &index) == B_OK; i++)
					choice.AddInt32("path", index);
				fReplicant.SendMessage(&choice);
			}
			break;

		case kMsgCheck:
			if (fReplicant.IsValid())
				break;
			if (fArea >= 0) {
				// The app quit: keep the last picture, wait for it again.
				_Disconnect();
			}
			if (!fStarting || system_time() - fStartTime > 5 * kCheckInterval)
				_Start();
			break;

		case B_MOUSE_WHEEL_CHANGED:
			if (fReplicant.IsValid()) {
				BMessage forward(kMsgRemoteReplicantEvent);
				BMessage event(*message);
				BPoint where;
				uint32 buttons;
				GetMouse(&where, &buttons, false);
				event.RemoveName("where");
				event.AddPoint("where", where);
				forward.AddMessage("event", &event);
				forward.AddPoint("screen_origin", ConvertToScreen(B_ORIGIN));
				fReplicant.SendMessage(&forward);
			}
			break;

		default:
			BView::MessageReceived(message);
	}
}


void
RemoteReplicantView::_Start()
{
	BMessenger app(fSignature.String());
	TRACE("start \"%s\" in %s: %s\n", Name(), fSignature.String(),
		app.IsValid() ? "running" : "not running");
	if (!app.IsValid())
		return;
	BMessage start(kMsgRemoteReplicantStart);
	start.AddMessage("archive", &fReplicantArchive);
	start.AddMessenger("host", BMessenger(this));
	start.AddPoint("screen_origin", ConvertToScreen(B_ORIGIN));
	// We show its menus: an app can't open one over us on Wayland.
	start.AddBool("export_menus", true);
	if (app.SendMessage(&start, (BHandler*)NULL, 0) == B_OK) {
		fStarting = true;
		fStartTime = system_time();
	}
}


void
RemoteReplicantView::_Started(BMessage* message)
{
	_Disconnect();
	fStarting = false;

	area_id source = message->GetInt32("area", -1);
	void* base = NULL;
	area_id area = clone_area("remote replicant", &base, B_ANY_ADDRESS,
		B_READ_AREA, source);
	TRACE("started; area %" B_PRId32 " cloned as %" B_PRId32 "\n", source,
		area);
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
	message->FindMessenger("replicant", &fReplicant);

	// Where the app runs from, to start it again after it quits
	char link[64];
	snprintf(link, sizeof(link), "/proc/%d/exe",
		(int)message->ReturnAddress().Team());
	char path[B_PATH_NAME_LENGTH];
	ssize_t length = readlink(link, path, sizeof(path) - 1);
	if (length > 0) {
		path[length] = '\0';
		fAppPath = path;
	}

	BRect bounds(0, 0, header->width - 1, header->height - 1);
	if (fBitmap == NULL || fBitmap->Bounds() != bounds) {
		delete fBitmap;
		fBitmap = new BBitmap(bounds, B_RGBA32);
	}
	_CopyPixels();
}


void
RemoteReplicantView::_Disconnect()
{
	if (fArea >= 0)
		delete_area(fArea);
	fArea = -1;
	fAreaBase = NULL;
	fReplicant = BMessenger();
}


void
RemoteReplicantView::_CopyPixels()
{
	if (fAreaBase == NULL || fBitmap == NULL)
		return;
	const remote_replicant_header* header
		= (const remote_replicant_header*)fAreaBase;
	const uint8* source = fAreaBase + kRemoteReplicantPixelOffset;
	uint8* target = (uint8*)fBitmap->Bits();
	int32 targetRow = fBitmap->BytesPerRow();
	int32 rowBytes = header->width * 4;
	if (rowBytes > targetRow)
		rowBytes = targetRow;
	int32 rows = fBitmap->Bounds().IntegerHeight() + 1;
	if (rows > header->height)
		rows = header->height;
	for (int32 y = 0; y < rows; y++) {
		memcpy(target + y * targetRow, source + y * header->bytes_per_row,
			rowBytes);
	}
	Invalidate();
}


/*static*/ void
RemoteReplicantView::_AddMenuItems(BMenu* menu, BMessage* archive,
	BMessage* path, BHandler* target)
{
	BMessage entry;
	for (int32 i = 0; archive->FindMessage("item", i, &entry) == B_OK; i++) {
		if (entry.GetBool("separator", false)) {
			menu->AddSeparatorItem();
			continue;
		}
		BMessage itemPath(*path);
		itemPath.AddInt32("path", i);
		BMessage submenuArchive;
		BMenuItem* item;
		if (entry.FindMessage("submenu", &submenuArchive) == B_OK) {
			BMenu* submenu = new BMenu(entry.GetString("label", ""));
			_AddMenuItems(submenu, &submenuArchive, &itemPath, target);
			item = new BMenuItem(submenu);
		} else {
			BMessage* choice = new BMessage(itemPath);
			choice->what = kMsgMenuChoice;
			item = new BMenuItem(entry.GetString("label", ""), choice);
			item->SetTarget(target);
		}
		item->SetEnabled(entry.GetBool("enabled", true));
		item->SetMarked(entry.GetBool("marked", false));
		menu->AddItem(item);
	}
}


void
RemoteReplicantView::_ShowMenu(BMessage* message)
{
	BMessage archive;
	if (message->FindMessage("menu", &archive) != B_OK)
		return;
	BPopUpMenu* menu = new BPopUpMenu(Name(), false, false);
	menu->SetFont(be_plain_font);
	BMessage path;
	_AddMenuItems(menu, &archive, &path, this);
	menu->SetAsyncAutoDestruct(true);
	// Below the item, as Deskbar's other tray menus open
	BPoint where = ConvertToScreen(Bounds().LeftBottom() + BPoint(0, 1));
	TRACE("showing a menu of %" B_PRId32 " items\n", menu->CountItems());
	menu->Go(where, true, true, ConvertToScreen(Bounds()), true);
}


void
RemoteReplicantView::_ShowHostMenu()
{
	BString appName = fAppPath.Length() > 0 ? BPath(fAppPath).Leaf()
		: fSignature.String();
	BString start;
	start.SetToFormat("Start %s", appName.String());
	BPopUpMenu* menu = new BPopUpMenu(Name(), false, false);
	menu->SetFont(be_plain_font);
	BMenuItem* item = new BMenuItem(start, new BMessage(kMsgStartApp));
	entry_ref ref;
	item->SetEnabled(fAppPath.Length() > 0
		|| be_roster->FindApp(fSignature.String(), &ref) == B_OK);
	menu->AddItem(item);
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Remove from Deskbar",
		new BMessage(kMsgRemoveSelf)));
	menu->SetTargetForItems(this);
	menu->SetAsyncAutoDestruct(true);
	menu->Go(ConvertToScreen(Bounds().LeftBottom() + BPoint(0, 1)), true, true,
		ConvertToScreen(Bounds()), true);
}


void
RemoteReplicantView::_Launch()
{
	if (be_roster->IsRunning(fSignature.String()))
		return;
	if (be_roster->Launch(fSignature.String()) == B_OK)
		return;
	// Not in the MIME database (an app run from where it was built, an
	// AppImage): start the executable it ran from last time.
	if (fAppPath.Length() == 0 || access(fAppPath.String(), X_OK) != 0)
		return;
	char* argv[] = { (char*)fAppPath.String(), NULL };
	pid_t pid;
	if (posix_spawn(&pid, fAppPath.String(), NULL, NULL, argv, environ) != 0)
		fprintf(stderr, "Deskbar: couldn't start %s\n", fAppPath.String());
}


void
RemoteReplicantView::_Forward(BPoint where)
{
	if (!fReplicant.IsValid() || Window() == NULL)
		return;
	BMessage* current = Window()->CurrentMessage();
	if (current == NULL)
		return;

	BMessage event(current->what);
	event.AddPoint("where", where);
	event.AddInt64("when", current->GetInt64("when", system_time()));
	event.AddInt32("buttons", current->GetInt32("buttons", fButtons));
	event.AddInt32("modifiers", current->GetInt32("modifiers", modifiers()));
	if (current->what == B_MOUSE_DOWN)
		event.AddInt32("clicks", current->GetInt32("clicks", 1));
	if (current->what == B_MOUSE_MOVED)
		event.AddInt32("be:transit", current->GetInt32("be:transit", 0));

	BMessage forward(kMsgRemoteReplicantEvent);
	forward.AddMessage("event", &event);
	forward.AddPoint("screen_origin", ConvertToScreen(B_ORIGIN));
	status_t status = fReplicant.SendMessage(&forward);
	if (current->what != B_MOUSE_MOVED)
		TRACE("forwarded %.4s: %s\n", (char*)&current->what, strerror(status));
}
