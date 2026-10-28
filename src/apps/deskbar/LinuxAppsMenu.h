/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef LINUX_APPS_MENU_H
#define LINUX_APPS_MENU_H


#include <Menu.h>


class BMenuItem;


const uint32 kLaunchLinuxApp = 'LXap';
	// "path" string: the app's .desktop file


/*!	The desktop's own applications, as the Plasma and Xfce menus list them:
	every .desktop file in the XDG data folders that the desktop would show,
	in submenus by freedesktop main category. Choosing one sends
	kLaunchLinuxApp to the Deskbar, which launches it with LaunchLinuxApp().
*/
class TLinuxAppsMenu : public BMenu {
public:
								TLinuxAppsMenu();

	virtual	void				AttachedToWindow();

	// the menu's item for the Deskbar menu, with the desktop's start icon
	static	BMenuItem*			NewItem();
	static	void				LaunchLinuxApp(const char* desktopFile);

private:
			void				_Build();

			bool				fBuilt;
};


#endif	// LINUX_APPS_MENU_H
