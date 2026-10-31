/*
 * Copyright 2011, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef DECORATOR_PRIVATE_H
#define DECORATOR_PRIVATE_H


#include <SupportDefs.h>


class BString;
class BWindow;


namespace BPrivate {


bool get_decorator(BString& name);
status_t set_decorator(const BString& name);
status_t preview_decorator(const BString& name, BWindow* window);

// Hokku: whether windows (and the pointer) cast a shadow
bool get_window_shadows();
status_t set_window_shadows(bool shadows);

// Cosmoe: tells the backend which decorator to draw this app's frames with.
status_t apply_decorator(void* display);


}	// namespace BPrivate


#endif	// DECORATOR_PRIVATE_H
