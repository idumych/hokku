/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Hokku: app_server's DesktopSettings as decorators read them, from the
 * settings the application has. It is ServerDesktopSettings in the binaries
 * (AppServerNames.h).
 */
#ifndef SERVER_DESKTOP_SETTINGS_H
#define SERVER_DESKTOP_SETTINGS_H


#include <InterfaceDefs.h>
#include <Menu.h>
#include <String.h>

#include "AppServerNames.h"
#include "ServerFont.h"


class Desktop;


#ifndef DESKTOP_SETTINGS_H
	// libbe's DesktopSettings.h has them too
enum {
	kAllSettings		= 0xff,
	kWorkspacesSettings	= 0x01,
	kFontSettings		= 0x02,
	kAppearanceSettings	= 0x04,
	kMouseSettings		= 0x08,
	kDraggerSettings	= 0x10,
};
#endif


class DesktopSettings {
public:
								DesktopSettings(Desktop* desktop);

			status_t			Save(uint32 mask = kAllSettings)
									{ return B_OK; }

			void				GetDefaultPlainFont(ServerFont& font) const;
			void				GetDefaultBoldFont(ServerFont& font) const;
			void				GetDefaultFixedFont(ServerFont& font) const;

			void				GetScrollBarInfo(scroll_bar_info& info) const;
			void				GetMenuInfo(menu_info& info) const;

			mode_mouse			MouseMode() const;
			mode_focus_follows_mouse FocusFollowsMouseMode() const;

			bool				NormalMouse() const
									{ return MouseMode() == B_NORMAL_MOUSE; }
			bool				FocusFollowsMouse() const
									{ return MouseMode()
										== B_FOCUS_FOLLOWS_MOUSE; }
			bool				ClickToFocusMouse() const
									{ return MouseMode()
										== B_CLICK_TO_FOCUS_MOUSE; }

			bool				AcceptFirstClick() const;
			bool				ShowAllDraggers() const;

			int32				WorkspacesCount() const;
			int32				WorkspacesColumns() const;
			int32				WorkspacesRows() const;

			rgb_color			UIColor(color_which which) const;

			bool				SubpixelAntialiasing() const { return true; }
			bool				CompositedDesktop() const { return true; }
			bool				CompositedShadows() const;
			bool				CompositedRoundedCorners() const;
			uint8				Hinting() const { return 1; }
			uint8				SubpixelAverageWeight() const { return 120; }
			bool				IsSubpixelOrderingRegular() const
									{ return true; }

			const BString&		ControlLook() const;

private:
			BString				fControlLook;
};


#endif	// SERVER_DESKTOP_SETTINGS_H
