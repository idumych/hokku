/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 */


#include "BrightnessItem.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include <gio/gio.h>

#include <Catalog.h>
#include <ControlLook.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <Screen.h>
#include <Slider.h>
#include <String.h>
#include <StringView.h>
#include <Window.h>

#include <DeskbarPrivate.h>
#include <WindowPrivate.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "BrightnessItem"


namespace {

const char* const kBacklightDirectory = "/sys/class/backlight";
const uint32 kMsgSliderChanged = 'brsc';
const uint32 kMsgSliderClosed = 'brcl';
const int32 kWheelStep = 5;
const int32 kMinimumLevel = 1;
	// never all the way to black, which would leave no way to see the slider


/*!	The screen's backlight, as /sys/class/backlight shows it. The level is
	set through logind, which lets the session's user do that without root.
*/
class Backlight {
public:
	Backlight()
		:
		fMax(0)
	{
		DIR* dir = opendir(kBacklightDirectory);
		if (dir == NULL)
			return;

		// Like systemd and brightnessctl: firmware (ACPI) over platform
		// drivers over the GPU's raw register
		int bestRank = 0;
		while (struct dirent* entry = readdir(dir)) {
			if (entry->d_name[0] == '.')
				continue;

			BString path;
			path.SetToFormat("%s/%s", kBacklightDirectory, entry->d_name);
			BString type = _ReadString(path, "type");
			int rank = type == "firmware" ? 3 : type == "platform" ? 2 : 1;
			int32 max = _ReadNumber(path, "max_brightness");
			if (max > 0 && rank > bestRank) {
				bestRank = rank;
				fName = entry->d_name;
				fPath = path;
				fMax = max;
			}
		}
		closedir(dir);
	}

	bool InitCheck() const
	{
		return fMax > 0;
	}

	int32 Level() const
	{
		if (fMax <= 0)
			return 0;

		int32 value = _ReadNumber(fPath, "brightness");
		return (int32)roundf(value * 100.0f / fMax);
	}

	void SetLevel(int32 level)
	{
		if (fMax <= 0)
			return;

		level = std::max(kMinimumLevel, std::min((int32)100, level));
		uint32 value = std::max((uint32)1,
			(uint32)roundf(level * fMax / 100.0f));

		GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);
		if (bus == NULL)
			return;

		// Sent at once by GDBus's own thread; the reply isn't needed
		g_dbus_connection_call(bus, "org.freedesktop.login1",
			"/org/freedesktop/login1/session/auto",
			"org.freedesktop.login1.Session", "SetBrightness",
			g_variant_new("(ssu)", "backlight", fName.String(), value),
			NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
		g_object_unref(bus);
	}

private:
	static BString _ReadString(const BString& path, const char* file)
	{
		BString result;
		BString filePath;
		filePath.SetToFormat("%s/%s", path.String(), file);
		FILE* stream = fopen(filePath.String(), "r");
		if (stream == NULL)
			return result;

		char buffer[64];
		if (fgets(buffer, sizeof(buffer), stream) != NULL) {
			result = buffer;
			result.Trim();
		}
		fclose(stream);
		return result;
	}

	static int32 _ReadNumber(const BString& path, const char* file)
	{
		BString value = _ReadString(path, file);
		return value.IsEmpty() ? -1 : atoi(value.String());
	}

	BString	fName;
	BString	fPath;
	int32	fMax;
};


/*!	The popup's content, with the thin dark edge a menu has. */
class SliderFrameView : public BGroupView {
public:
	SliderFrameView()
		:
		BGroupView(B_VERTICAL, B_USE_SMALL_SPACING)
	{
		SetFlags(Flags() | B_WILL_DRAW);
	}

	virtual void Draw(BRect updateRect)
	{
		BGroupView::Draw(updateRect);
		SetHighColor(tint_color(ViewColor(), B_DARKEN_3_TINT));
		StrokeRect(Bounds());
	}
};


/*!	The slider, in a popup beside the tray icon: it takes the pointer and
	keyboard, and closes when the user clicks elsewhere or presses Escape.
*/
class BrightnessWindow : public BWindow {
public:
	BrightnessWindow(const BMessenger& item, BWindow* parent)
		:
		BWindow(BRect(0, 0, 100, 40), B_TRANSLATE("Brightness"),
			B_NO_BORDER_WINDOW_LOOK, kMenuWindowFeel,
			B_NOT_MOVABLE | B_NOT_RESIZABLE | B_NOT_ZOOMABLE
				| B_NOT_MINIMIZABLE | B_ASYNCHRONOUS_CONTROLS
				| B_CLOSE_ON_ESCAPE | kWindowPopupGrabFlag),
		fItem(item)
	{
		fSlider = new BSlider("brightness", NULL,
			new BMessage(kMsgSliderChanged), kMinimumLevel, 100,
			B_HORIZONTAL);
		fSlider->SetModificationMessage(new BMessage(kMsgSliderChanged));
		fSlider->SetValue(fBacklight.Level());
		fSlider->SetExplicitMinSize(BSize(
			be_control_look->DefaultLabelSpacing() * 30, B_SIZE_UNSET));

		fLevelView = new BStringView("level", "");
		fLevelView->SetAlignment(B_ALIGN_RIGHT);
		fLevelView->SetExplicitMinSize(BSize(
			fLevelView->StringWidth("100%"), B_SIZE_UNSET));

		BStringView* title = new BStringView("title",
			B_TRANSLATE("Brightness"));
		title->SetFont(be_bold_font);

		SliderFrameView* frame = new SliderFrameView();
		BLayoutBuilder::Group<>(frame)
			.SetInsets(B_USE_SMALL_INSETS)
			.AddGroup(B_HORIZONTAL)
				.Add(title)
				.AddGlue()
				.Add(fLevelView)
			.End()
			.Add(fSlider);

		BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
			.Add(frame);

		_UpdateLevelView();

		BSize size = GetLayout()->PreferredSize();
		ResizeTo(size.width, size.height);
		BPrivate::set_popup_window_parent(this, parent);
	}

	virtual void Show()
	{
		BWindow::Show();
		if (Lock()) {
			fSlider->MakeFocus(true);
			Unlock();
		}
	}

	virtual void MessageReceived(BMessage* message)
	{
		switch (message->what) {
			case kMsgSliderChanged:
				fBacklight.SetLevel(fSlider->Value());
				_UpdateLevelView();
				break;

			default:
				BWindow::MessageReceived(message);
				break;
		}
	}

	virtual void Quit()
	{
		fItem.SendMessage(kMsgSliderClosed);
		BWindow::Quit();
	}

private:
	void _UpdateLevelView()
	{
		BString level;
		level.SetToFormat("%" B_PRId32 "%%", fSlider->Value());
		fLevelView->SetText(level.String());
	}

	BMessenger		fItem;
	Backlight		fBacklight;
	BSlider*		fSlider;
	BStringView*	fLevelView;
};

}	// namespace


BrightnessItem::BrightnessItem(BRect frame)
	:
	BView(frame, "BrightnessItem", B_FOLLOW_LEFT | B_FOLLOW_TOP,
		B_WILL_DRAW),
	fSliderClosed(0)
{
}


BrightnessItem::BrightnessItem(BMessage* archive)
	:
	BView(archive),
	fSliderClosed(0)
{
}


BrightnessItem::~BrightnessItem()
{
	fSlider.SendMessage(B_QUIT_REQUESTED);
}


bool
BrightnessItem::HasBacklight()
{
	return Backlight().InitCheck();
}


BArchivable*
BrightnessItem::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, "BrightnessItem"))
		return NULL;

	return new BrightnessItem(archive);
}


status_t
BrightnessItem::Archive(BMessage* into, bool deep) const
{
	status_t status = BView::Archive(into, deep);
	if (status != B_OK)
		return status;

	return into->AddString("add_on", kDeskbarSignature);
}


void
BrightnessItem::AttachedToWindow()
{
	BView::AttachedToWindow();
	AdoptParentColors();
	_UpdateToolTip();
}


void
BrightnessItem::Draw(BRect updateRect)
{
	// A sun: a disc and eight rays, scaled to the tray's icon size
	BRect bounds = Bounds();
	float size = std::min(bounds.Width(), bounds.Height()) + 1;
	BPoint center(bounds.left + bounds.Width() / 2,
		bounds.top + bounds.Height() / 2);
	float radius = size * 0.2f;

	SetDrawingMode(B_OP_ALPHA);
	SetPenSize(std::max(1.0f, roundf(size / 14)));
	SetLineMode(B_ROUND_CAP, B_ROUND_JOIN);

	rgb_color outline = make_color(110, 80, 0);
	SetHighColor(outline);
	for (int i = 0; i < 8; i++) {
		float angle = i * M_PI / 4;
		BPoint direction(cosf(angle), sinf(angle));
		StrokeLine(center + BPoint(direction.x * radius * 1.55f,
				direction.y * radius * 1.55f),
			center + BPoint(direction.x * size * 0.45f,
				direction.y * size * 0.45f));
	}

	SetHighColor(255, 200, 30);
	FillEllipse(center, radius, radius);
	SetHighColor(outline);
	StrokeEllipse(center, radius, radius);

	SetPenSize(1);
	SetDrawingMode(B_OP_COPY);
}


void
BrightnessItem::MouseDown(BPoint where)
{
	if (fSlider.IsValid()) {
		fSlider.SendMessage(B_QUIT_REQUESTED);
		return;
	}

	// The click that closed the slider (outside it) can reach the icon
	// right after: don't open it again
	if (system_time() - fSliderClosed < 300000)
		return;

	_ShowSlider();
}


void
BrightnessItem::MouseMoved(BPoint where, uint32 transit,
	const BMessage* dragMessage)
{
	if (transit == B_ENTERED_VIEW)
		_UpdateToolTip();

	BView::MouseMoved(where, transit, dragMessage);
}


void
BrightnessItem::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_MOUSE_WHEEL_CHANGED:
		{
			float delta;
			if (message->FindFloat("be:wheel_delta_y", &delta) != B_OK
				|| delta == 0)
				break;

			Backlight backlight;
			int32 level = backlight.Level();
			level = delta < 0 ? level + kWheelStep : level - kWheelStep;
			backlight.SetLevel(level);

			BString toolTip;
			toolTip.SetToFormat(B_TRANSLATE("Brightness: %" B_PRId32 "%%"),
				std::max(kMinimumLevel, std::min((int32)100, level)));
			SetToolTip(toolTip.String());
			break;
		}

		case kMsgSliderClosed:
			fSlider = BMessenger();
			fSliderClosed = system_time();
			_UpdateToolTip();
			break;

		default:
			BView::MessageReceived(message);
			break;
	}
}


void
BrightnessItem::_ShowSlider()
{
	BrightnessWindow* window = new BrightnessWindow(BMessenger(this),
		Window());
	fSlider = BMessenger(window);

	// It would show a stale level beside the slider; back when it closes
	SetToolTip((const char*)NULL);

	// Below the icon, its right edge at the icon's, or above it when the
	// Deskbar is at the bottom; on Wayland the compositor keeps it on screen
	BRect icon = ConvertToScreen(Bounds());
	BRect screen = BScreen(Window()).Frame();
	float width = window->Bounds().Width();
	float height = window->Bounds().Height();
	BPoint where(icon.right - width, icon.bottom + 4);
	if (where.y + height > screen.bottom)
		where.y = icon.top - 4 - height;
	if (where.x < screen.left)
		where.x = icon.left;

	// A popup's backend window is made when it's moved after Show()
	window->Show();
	window->MoveTo(where);
}


void
BrightnessItem::_UpdateToolTip()
{
	BString toolTip;
	toolTip.SetToFormat(B_TRANSLATE("Brightness: %" B_PRId32 "%%"),
		Backlight().Level());
	SetToolTip(toolTip.String());
}
