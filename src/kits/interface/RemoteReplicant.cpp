/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 *
 * The app side of remote replicants (see RemoteReplicant.h): the replicant
 * runs in a window of the app's own that never goes on screen, and what it
 * draws is copied into a shared area for the host to show.
 */


#include <RemoteReplicant.h>

#include <Archivable.h>
#include <Message.h>
#include <Messenger.h>
#include <OS.h>
#include <View.h>
#include <Window.h>

#include <cairo.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <AppMisc.h>


namespace BPrivate {


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
		fprintf(stderr, "RemoteReplicant[%d]: ", (int)getpid()); \
		fprintf(stderr, __VA_ARGS__); \
	} \
} while (0)


class RemoteReplicantWindow : public BWindow {
public:
								RemoteReplicantWindow(BView* replicant,
									const BMessenger& host, BPoint origin);
	virtual						~RemoteReplicantWindow();

			status_t			InitCheck() const { return fArea; }
			area_id				Area() const { return fArea; }

	virtual	void				DispatchMessage(BMessage* message,
									BHandler* handler);
	virtual	void				MessageReceived(BMessage* message);

private:
			void				_SetOrigin(BPoint origin);
			void				_Publish();

			BView*				fReplicant;
			BMessenger			fHost;
			area_id				fArea;
			uint8*				fAreaBase;
			size_t				fAreaSize;
};


RemoteReplicantWindow::RemoteReplicantWindow(BView* replicant,
	const BMessenger& host, BPoint origin)
	:
	// The offscreen constructor: no window in the display server, but a
	// looper of its own once Run() is called, so the replicant's messages,
	// message runners and Invalidate() work as in any window.
	BWindow(replicant->Bounds().OffsetToCopy(origin), 0),
	fReplicant(replicant),
	fHost(host),
	fArea(-1),
	fAreaBase(NULL),
	fAreaSize(0)
{
	// Not a bitmap's window: draw through the normal _UPDATE_ cycle, and
	// count as shown (a hidden window's views don't draw), without the
	// backend window Show() would make.
	fInTransaction = false;
	fShowLevel = 0;
	SetTitle("remote replicant");

	int32 width = replicant->Bounds().IntegerWidth() + 1;
	int32 height = replicant->Bounds().IntegerHeight() + 1;
	size_t bytesPerRow = width * 4;
	size_t size = kRemoteReplicantPixelOffset + bytesPerRow * height;
	size = (size + B_PAGE_SIZE - 1) & ~(B_PAGE_SIZE - 1);
	void* base = NULL;
	fArea = create_area("remote replicant", &base, B_ANY_ADDRESS, size,
		B_NO_LOCK, B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA);
	if (fArea >= 0) {
		fAreaBase = (uint8*)base;
		fAreaSize = size;
		remote_replicant_header* header = (remote_replicant_header*)base;
		memset(base, 0, size);
		header->magic = kRemoteReplicantMagic;
		header->width = width;
		header->height = height;
		header->bytes_per_row = bytesPerRow;
	}

	replicant->MoveTo(0, 0);
	AddChild(replicant);
}


RemoteReplicantWindow::~RemoteReplicantWindow()
{
	TRACE("stopped \"%s\"\n", fReplicant->Name());
	if (fArea >= 0)
		delete_area(fArea);
}


void
RemoteReplicantWindow::DispatchMessage(BMessage* message, BHandler* handler)
{
	bool update = message->what == _UPDATE_;
	if (message->what == B_MOUSE_DOWN || message->what == B_MOUSE_UP) {
		TRACE("dispatching %.4s to %s\n", (char*)&message->what,
			handler != NULL ? handler->Name() : "(none)");
	}
	BWindow::DispatchMessage(message, handler);
	if (update)
		_Publish();
}


void
RemoteReplicantWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgRemoteReplicantEvent:
		{
			BPoint origin;
			if (message->FindPoint("screen_origin", &origin) == B_OK)
				_SetOrigin(origin);
			BMessage event;
			if (message->FindMessage("event", &event) != B_OK)
				break;
			// As the backend sends them: window coordinates, which are the
			// replicant's, and the view it is for.
			BPoint where;
			if (event.FindPoint("where", &where) == B_OK) {
				event.RemoveName("where");
				event.RemoveName("window_where");
				event.AddPoint("window_where", where);
				event.RemoveName("screen_where");
				event.AddPoint("screen_where", where + origin);
			}
			event.RemoveName("_view_token");
			event.AddInt32("_view_token", _get_object_token_(fReplicant));
			if (event.what != B_MOUSE_MOVED) {
				TRACE("event %.4s at %g,%g, screen origin %g,%g\n",
					(char*)&event.what, where.x, where.y, origin.x, origin.y);
			}
			// To the replicant itself: a message posted without a handler
			// would go to the window.
			PostMessage(&event, fReplicant);
			break;
		}

		case kMsgRemoteReplicantMoved:
		{
			BPoint origin;
			if (message->FindPoint("screen_origin", &origin) == B_OK)
				_SetOrigin(origin);
			break;
		}

		case kMsgRemoteReplicantStop:
			Quit();
			break;

		default:
			BWindow::MessageReceived(message);
	}
}


void
RemoteReplicantWindow::_SetOrigin(BPoint origin)
{
	// Where the host shows the replicant, so ConvertToScreen() and menus
	// opened at a converted point land on it. Only the frame changes:
	// there is no window to move.
	fFrame.OffsetTo(origin);
}


void
RemoteReplicantWindow::_Publish()
{
	if (fAreaBase == NULL || fBackingSurface == NULL)
		return;

	remote_replicant_header* header = (remote_replicant_header*)fAreaBase;
	pthread_mutex_lock(&fBackingSurfaceLock);
	cairo_surface_flush(fBackingSurface);
	const uint8* source = cairo_image_surface_get_data(fBackingSurface);
	int32 sourceStride = cairo_image_surface_get_stride(fBackingSurface);
	int32 width = cairo_image_surface_get_width(fBackingSurface);
	int32 height = cairo_image_surface_get_height(fBackingSurface);
	if (width > header->width)
		width = header->width;
	if (height > header->height)
		height = header->height;

	uint8* target = fAreaBase + kRemoteReplicantPixelOffset;
	int32 opaque = 0;
	for (int32 y = 0; source != NULL && y < height; y++) {
		const uint32* in = (const uint32*)(source + y * sourceStride);
		uint8* out = target + y * header->bytes_per_row;
		for (int32 x = 0; x < width; x++) {
			// cairo's ARGB32 is premultiplied; B_RGBA32 isn't.
			uint32 pixel = in[x];
			uint8 alpha = pixel >> 24;
			uint8 red = (pixel >> 16) & 0xff;
			uint8 green = (pixel >> 8) & 0xff;
			uint8 blue = pixel & 0xff;
			if (alpha != 0 && alpha != 255) {
				red = red * 255 / alpha;
				green = green * 255 / alpha;
				blue = blue * 255 / alpha;
			}
			out[x * 4 + 0] = blue;
			out[x * 4 + 1] = green;
			out[x * 4 + 2] = red;
			out[x * 4 + 3] = alpha;
			if (alpha != 0)
				opaque++;
		}
	}
	pthread_mutex_unlock(&fBackingSurfaceLock);
	atomic_add(&header->sequence, 1);
	if (header->sequence == 1) {
		TRACE("first picture %" B_PRId32 "x%" B_PRId32 ", %" B_PRId32 " pixels "
			"not transparent\n", width, height, opaque);
	}

	// Never wait for the host; if it's gone, so is the replicant.
	status_t status = fHost.SendMessage(kMsgRemoteReplicantPixels,
		(BHandler*)NULL);
	if (status == B_BAD_PORT_ID || !fHost.IsValid())
		PostMessage(kMsgRemoteReplicantStop);
}


void
start_remote_replicant(BMessage* request)
{
	BMessenger host;
	BMessage archive;
	if (request->FindMessenger("host", &host) != B_OK
		|| request->FindMessage("archive", &archive) != B_OK)
		return;
	BPoint origin(0, 0);
	request->FindPoint("screen_origin", &origin);

	TRACE("start \"%s\" (class %s)\n", archive.GetString("_name", "?"),
		archive.GetString("class", "?"));
	BArchivable* object = instantiate_object(&archive);
	BView* view = dynamic_cast<BView*>(object);
	if (view == NULL) {
		TRACE("can't instantiate it: %s\n", strerror(errno));
		delete object;
		BMessage failed(kMsgRemoteReplicantFailed);
		failed.AddInt32("error", B_BAD_TYPE);
		host.SendMessage(&failed);
		return;
	}

	RemoteReplicantWindow* window = new RemoteReplicantWindow(view, host,
		origin);
	if (window->InitCheck() < B_OK) {
		BMessage failed(kMsgRemoteReplicantFailed);
		failed.AddInt32("error", window->InitCheck());
		host.SendMessage(&failed);
		window->Quit();
		return;
	}

	BMessage started(kMsgRemoteReplicantStarted);
	started.AddMessenger("replicant", BMessenger(window));
	started.AddInt32("area", window->Area());
	started.AddInt32("width", view->Bounds().IntegerWidth() + 1);
	started.AddInt32("height", view->Bounds().IntegerHeight() + 1);
	window->Run();
	status_t status = host.SendMessage(&started);
	TRACE("running in area %" B_PRId32 ", told the host: %s\n",
		window->Area(), strerror(status));

	// Draw once now that the host knows where to look.
	if (window->Lock()) {
		view->Invalidate();
		window->Unlock();
	}
}


}	// namespace BPrivate
