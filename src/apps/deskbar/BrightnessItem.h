/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef BRIGHTNESS_ITEM_H
#define BRIGHTNESS_ITEM_H


#include <Messenger.h>
#include <View.h>


/*!	The Deskbar tray's brightness control, on Linux machines with a
	backlight: a click opens a slider beside it, the mouse wheel over it
	steps the brightness, and its tooltip tells the level. The Deskbar adds
	it itself when it starts.
*/
class BrightnessItem : public BView {
public:
								BrightnessItem(BRect frame);
								BrightnessItem(BMessage* archive);
	virtual						~BrightnessItem();

	static	bool				HasBacklight();

	static	BArchivable*		Instantiate(BMessage* archive);
	virtual	status_t			Archive(BMessage* into, bool deep = true) const;

	virtual	void				AttachedToWindow();
	virtual	void				Draw(BRect updateRect);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* dragMessage);
	virtual	void				MessageReceived(BMessage* message);

private:
			void				_ShowSlider();
			void				_UpdateToolTip();

			BMessenger			fSlider;
			bigtime_t			fSliderClosed;
};


#endif	// BRIGHTNESS_ITEM_H
