/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 */


#include "ThemesView.h"

#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <Directory.h>
#include <Entry.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>
#include <Window.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Themes"


static const uint32 kMsgSelect = 'thSl';
static const uint32 kMsgApply = 'thAy';
static const uint32 kMsgAskName = 'thAs';
static const uint32 kMsgSave = 'thSv';
static const uint32 kMsgDelete = 'thDl';


/*!	Asks for the name of a theme to save the current look as. */
class SaveThemeWindow : public BWindow {
public:
	SaveThemeWindow(BView* target, BRect parentFrame, const char* name)
		:
		BWindow(BRect(0, 0, 300, 100), B_TRANSLATE("Save theme"),
			B_MODAL_WINDOW_LOOK, B_MODAL_APP_WINDOW_FEEL,
			B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS
				| B_CLOSE_ON_ESCAPE),
		fTarget(target)
	{
		fName = new BTextControl(B_TRANSLATE("Name:"), name, NULL);
		BButton* save = new BButton(B_TRANSLATE("Save"),
			new BMessage(kMsgSave));
		BButton* cancel = new BButton(B_TRANSLATE("Cancel"),
			new BMessage(B_QUIT_REQUESTED));

		BLayoutBuilder::Group<>(this, B_VERTICAL)
			.SetInsets(B_USE_WINDOW_SPACING)
			.Add(new BStringView("text",
				B_TRANSLATE("Save the look in use now as a theme:")))
			.Add(fName)
			.AddGroup(B_HORIZONTAL)
				.AddGlue()
				.Add(cancel)
				.Add(save)
			.End();

		SetDefaultButton(save);
		fName->MakeFocus(true);
		fName->TextView()->SelectAll();
		CenterIn(parentFrame);
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what != kMsgSave) {
			BWindow::MessageReceived(message);
			return;
		}

		BString name(fName->Text());
		name.Trim();
		if (name.IsEmpty() || name.FindFirst('/') >= 0) {
			BAlert* alert = new BAlert(B_TRANSLATE("Save theme"),
				B_TRANSLATE("A theme needs a name, without a slash (/)."),
				B_TRANSLATE("OK"));
			alert->Go(NULL);
			return;
		}

		BMessage save(kMsgSave);
		save.AddString("name", name);
		BMessenger(fTarget).SendMessage(&save);
		Quit();
	}

private:
	BView*			fTarget;
	BTextControl*	fName;
};


ThemesView::ThemesView(const char* name)
	:
	BView(name, 0)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	fList = new BListView("themes");
	fList->SetSelectionMessage(new BMessage(kMsgSelect));
	fList->SetInvocationMessage(new BMessage(kMsgApply));
	BScrollView* scroll = new BScrollView("themes scroll", fList, 0, false,
		true);

	fDescription = new BTextView("description");
	fDescription->MakeEditable(false);
	fDescription->MakeSelectable(false);
	fDescription->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fDescription->SetExplicitMinSize(BSize(B_SIZE_UNSET, 60));

	fApplyButton = new BButton(B_TRANSLATE("Apply"), new BMessage(kMsgApply));
	fSaveButton = new BButton(B_TRANSLATE("Save current look" B_UTF8_ELLIPSIS),
		new BMessage(kMsgAskName));
	fDeleteButton = new BButton(B_TRANSLATE("Delete"),
		new BMessage(kMsgDelete));

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(scroll, 3)
		.Add(fDescription, 1)
		.AddGroup(B_HORIZONTAL)
			.Add(fApplyButton)
			.Add(fSaveButton)
			.AddGlue()
			.Add(fDeleteButton)
		.End();
}


void
ThemesView::AttachedToWindow()
{
	fList->SetTarget(this);
	fApplyButton->SetTarget(this);
	fSaveButton->SetTarget(this);
	fDeleteButton->SetTarget(this);

	_Reload();
}


void
ThemesView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgSelect:
			_UpdateControls();
			break;

		case kMsgApply:
			_Apply();
			break;

		case kMsgAskName:
		{
			Theme* theme = _Selected();
			BString name = theme != NULL && theme->IsUserTheme()
				? theme->name : BString(B_TRANSLATE("My theme"));
			(new SaveThemeWindow(this, Window()->Frame(), name.String()))
				->Show();
			break;
		}

		case kMsgSave:
		{
			const char* name;
			if (message->FindString("name", &name) == B_OK)
				_Save(name);
			break;
		}

		case kMsgDelete:
			_Delete();
			break;

		default:
			BView::MessageReceived(message);
			break;
	}
}


void
ThemesView::_Reload(const char* select)
{
	BString current = current_theme_name();
	if (select == NULL)
		select = current.String();

	while (fList->CountItems() > 0)
		delete fList->RemoveItem((int32)0);
	find_themes(fThemes);

	int32 selection = -1;
	for (int32 i = 0; i < fThemes.CountItems(); i++) {
		Theme* theme = fThemes.ItemAt(i);
		BString label(theme->name);
		if (theme->IsUserTheme())
			label << " " << B_TRANSLATE("(yours)");
		if (theme->name == current)
			label << "  " << B_TRANSLATE("— in use");
		fList->AddItem(new BStringItem(label));
		if (theme->name == select)
			selection = i;
	}

	fList->Select(selection);
	_UpdateControls();
}


Theme*
ThemesView::_Selected() const
{
	return fThemes.ItemAt(fList->CurrentSelection());
}


void
ThemesView::_UpdateControls()
{
	Theme* theme = _Selected();
	fDescription->SetText(theme != NULL ? theme->description.String() : "");
	fApplyButton->SetEnabled(theme != NULL);
	fDeleteButton->SetEnabled(theme != NULL && theme->IsUserTheme());
}


void
ThemesView::_Apply()
{
	Theme* theme = _Selected();
	if (theme == NULL)
		return;

	theme->Apply();
	BString name(theme->name);
	_Reload(name.String());
	Window()->PostMessage(kMsgThemeApplied);
}


void
ThemesView::_Save(const char* name)
{
	Theme theme;
	theme.Capture(name);

	BPath folder;
	user_themes_folder(folder, true);
	folder.Append(name);
	status_t status = theme.Save(folder);
	if (status != B_OK) {
		BString text(B_TRANSLATE("The theme couldn't be saved:"));
		text << "\n" << strerror(status);
		BAlert* alert = new BAlert(B_TRANSLATE("Save theme"), text,
			B_TRANSLATE("OK"));
		alert->Go(NULL);
		return;
	}

	set_current_theme_name(name);
	_Reload(name);
}


void
ThemesView::_Delete()
{
	Theme* theme = _Selected();
	if (theme == NULL || !theme->IsUserTheme())
		return;

	BString text;
	text.SetToFormat(B_TRANSLATE("Delete the theme \"%s\"?"),
		theme->name.String());
	BAlert* alert = new BAlert(B_TRANSLATE("Delete theme"), text,
		B_TRANSLATE("Cancel"), B_TRANSLATE("Delete"), NULL,
		B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	if (alert->Go() != 1)
		return;

	// the folder holds theme.ini and the files it names, nothing deeper
	BDirectory folder(theme->Folder().Path());
	BEntry entry;
	while (folder.GetNextEntry(&entry) == B_OK)
		entry.Remove();
	BEntry(theme->Folder().Path()).Remove();
	_Reload();
}
