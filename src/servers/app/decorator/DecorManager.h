/*
 * Copyright (c) 2001-2005, Haiku, Inc.
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT license.
 *
 * Author:
 *		DarkWyrm <bpmagic@columbus.rr.com>
 *		Clemens Zeidler <haiku@clemens-zeidler.de>
 *		Joseph Groover <looncraz@satx.rr.com>
 *
 * Hokku: a decorator add-on's entry point, as app_server has it. Hokku has no
 * window behaviours or desktop listeners of add-ons: the Linux desktop moves
 * and stacks windows.
 */
#ifndef DECOR_MANAGER_H
#define DECOR_MANAGER_H


#include <image.h>
#include <String.h>
#include <Locker.h>
#include <ObjectList.h>
#include <Entry.h>
#include <DecorInfo.h>

#include "Decorator.h"

class Desktop;
class DesktopListener {
public:
	virtual						~DesktopListener() {}
};
class DrawingEngine;
class Window;
class WindowBehaviour;


typedef BObjectList<DesktopListener> DesktopListenerList;


class DecorAddOn {
public:
								DecorAddOn(image_id id, const char* name);
	virtual						~DecorAddOn();

	virtual status_t			InitCheck() const;

			image_id			ImageID() const { return fImageID; }

			Decorator*			AllocateDecorator(Desktop* desktop,
									DrawingEngine* engine, BRect rect,
									const char* title, window_look look,
									uint32 flags);

	virtual	WindowBehaviour*	AllocateWindowBehaviour(Window* window);

	virtual const DesktopListenerList& GetDesktopListeners();

protected:
	virtual	Decorator*			_AllocateDecorator(DesktopSettings& settings,
									BRect rect, Desktop* desktop);

			DesktopListenerList	fDesktopListeners;

private:
			image_id			fImageID;
			BString 			fName;
};


#endif	/* DECOR_MANAGER_H */
