/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Hokku: what decorators ask of app_server's Desktop. There is one per
 * application, standing for the desktop its windows are on; the compositor
 * is the Linux desktop's, which is always there.
 */
#ifndef DESKTOP_H
#define DESKTOP_H


#include <SupportDefs.h>


class Compositor;


class Desktop {
public:
								Desktop();
								~Desktop();

	static	Desktop*			Default();

			bool				LockSingleWindow() { return true; }
			void				UnlockSingleWindow() {}
			bool				LockAllWindows() { return true; }
			void				UnlockAllWindows() {}

			// Not NULL: the Linux desktop composites windows. Decorators
			// only compare it with NULL.
			Compositor*			GetCompositor() const
									{ return (Compositor*)this; }

			// Decorators call this when settings they keep change, for new
			// windows to show them.
			void				ReloadDecor();
};


#endif	// DESKTOP_H
