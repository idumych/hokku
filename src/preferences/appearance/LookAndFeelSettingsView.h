/*
 *  Copyright 2010-2020 Haiku, Inc. All rights reserved.
 *  Distributed under the terms of the MIT license.
 *
 *	Authors:
 *		Stephan Aßmus <superstippi@gmx.de>
 *		Alexander von Gluck <kallisti5@unixzen.com>
 *		Ryan Leavengood <leavengood@gmail.com>
 *		John Scipione <jscipione@gmail.com>
 */
#ifndef LOOK_AND_FEEL_SETTINGS_VIEW_H
#define LOOK_AND_FEEL_SETTINGS_VIEW_H


#include <String.h>
#include <View.h>


class BButton;
class BCheckBox;
class BMenuField;
class BPopUpMenu;
class FakeScrollBar;

class LookAndFeelSettingsView : public BView {
public:
								LookAndFeelSettingsView(const char* name);
	virtual						~LookAndFeelSettingsView();

	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* message);

			bool				IsDefaultable();
			void				SetDefaults();

			bool				IsRevertable();
			void				Revert();

private:
			void				_SetControlLook(const BString& path);
			void				_BuildControlLookMenu();
			BString				_ControlLookLabel(const char* name);

			bool				_DoubleScrollBarArrows();
			void				_SetDoubleScrollBarArrows(bool doubleArrows);

			void				_SetShadows(bool shadows);

private:
			BButton*			fControlLookInfoButton;
			BMenuField*			fControlLookMenuField;
			BPopUpMenu*			fControlLookMenu;

			FakeScrollBar*		fArrowStyleSingle;
			FakeScrollBar*		fArrowStyleDouble;

			BCheckBox*			fShadowsCheckBox;

			BString				fSavedControlLook;
			BString				fCurrentControlLook;

			bool				fSavedDoubleArrowsValue : 1;
			bool				fSavedShadows : 1;
};


#endif // LOOK_AND_FEEL_SETTINGS_VIEW_H
