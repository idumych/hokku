// BitmapBitsSyncTest: direct writes to Bits() of a B_BITMAP_ACCEPTS_VIEWS bitmap
// and drawing by its views must both survive, as on Haiku, where they share
// memory. Run it with a display (e.g. Xvfb and COSMOE_BACKEND=x11); it prints
// PASS/FAIL per check and exits 1 on a failure.
#include <Application.h>
#include <Bitmap.h>
#include <View.h>

#include <stdio.h>
#include <string.h>

static int sFailures = 0;

static uint32
PixelAt(BBitmap* bitmap, int x, int y)
{
	const uint8* bits = (const uint8*)bitmap->Bits();
	return *(const uint32*)(bits + y * bitmap->BytesPerRow() + x * 4);
}

static void
Fill(BBitmap* bitmap, uint32 color)
{
	uint8* bits = (uint8*)bitmap->Bits();
	int height = bitmap->Bounds().IntegerHeight() + 1;
	int width = bitmap->Bounds().IntegerWidth() + 1;
	for (int y = 0; y < height; y++) {
		uint32* row = (uint32*)(bits + y * bitmap->BytesPerRow());
		for (int x = 0; x < width; x++)
			row[x] = color;
	}
}

static void
Check(const char* what, uint32 got, uint32 expected)
{
	// Compare RGB only; the alpha handling is a separate question.
	bool ok = (got & 0x00ffffff) == (expected & 0x00ffffff);
	printf("%s %s: got %08x, expected %08x\n", ok ? "PASS" : "FAIL", what,
		(unsigned)got, (unsigned)expected);
	if (!ok)
		sFailures++;
}

int
main()
{
	BApplication app("application/x-vnd.Hokku-BitmapSyncTest");
	const uint32 kRed = 0xffff0000, kBlue = 0xff0000ff, kGreen = 0xff00ff00;

	// 1. Bits written directly are what the views draw on top of.
	BBitmap* bitmap = new BBitmap(BRect(0, 0, 19, 19), B_BITMAP_ACCEPTS_VIEWS,
		B_RGBA32);
	BView* view = new BView(bitmap->Bounds(), "v", B_FOLLOW_NONE, 0);
	bitmap->AddChild(view);
	Fill(bitmap, kRed);
	bitmap->Lock();
	view->SetHighColor(0, 0, 255);
	view->FillRect(BRect(0, 0, 4, 4));
	view->Sync();
	bitmap->Unlock();
	Check("view drawing kept", PixelAt(bitmap, 2, 2), kBlue);
	Check("direct writes kept under view drawing", PixelAt(bitmap, 10, 10),
		kRed);

	// 2. Writing to Bits() after the views drew, then reading again, keeps
	// the write (the second Bits() must not copy the surface back over it).
	((uint32*)bitmap->Bits())[15] = kGreen;
	Check("direct write after drawing kept", PixelAt(bitmap, 15, 0), kGreen);

	// 3. DrawBitmap of a view bitmap shows what was written to its Bits().
	BBitmap* source = new BBitmap(BRect(0, 0, 9, 9), B_BITMAP_ACCEPTS_VIEWS,
		B_RGBA32);
	source->AddChild(new BView(source->Bounds(), "s", B_FOLLOW_NONE, 0));
	Fill(source, kGreen);
	BBitmap* target = new BBitmap(BRect(0, 0, 9, 9), B_BITMAP_ACCEPTS_VIEWS,
		B_RGBA32);
	BView* targetView = new BView(target->Bounds(), "t", B_FOLLOW_NONE, 0);
	target->AddChild(targetView);
	Fill(target, kRed);
	target->Lock();
	targetView->SetDrawingMode(B_OP_ALPHA);
	targetView->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	targetView->DrawBitmap(source, source->Bounds(), target->Bounds());
	targetView->Sync();
	target->Unlock();
	Check("DrawBitmap shows the source's direct writes", PixelAt(target, 5, 5),
		kGreen);

	delete source;
	delete target;
	delete bitmap;
	printf("%s\n", sFailures == 0 ? "ALL PASSED" : "FAILED");
	return sFailures == 0 ? 0 : 1;
}
