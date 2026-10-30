/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef THEMES_VIEW_H
#define THEMES_VIEW_H


#include <View.h>

#include "Theme.h"


class BButton;
class BListView;
class BTextView;


// Posted to the window after a theme is applied, so the other tabs show it.
static const uint32 kMsgThemeApplied = 'thAp';


class ThemesView : public BView {
public:
								ThemesView(const char* name);

	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* message);

private:
			void				_Reload(const char* select = NULL);
			Theme*				_Selected() const;
			void				_UpdateControls();
			void				_Apply();
			void				_Save(const char* name);
			void				_Delete();

			ThemeList			fThemes;
			BListView*			fList;
			BTextView*			fDescription;
			BButton*			fApplyButton;
			BButton*			fSaveButton;
			BButton*			fDeleteButton;
};


#endif	// THEMES_VIEW_H
