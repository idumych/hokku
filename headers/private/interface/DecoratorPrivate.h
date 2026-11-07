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

// Hokku: whether window frames round their corners, where the decorator
// can (a decorator add-on that is a RoundedDecorator, or Haiku's)
bool get_window_rounded_corners();
status_t set_window_rounded_corners(bool rounded);

// Hokku: Dano's ZSnake, a band joining a submenu's selection to the item
// it hangs from (menus opened afterwards show it)
bool get_menu_snake();
status_t set_menu_snake(bool snake);

// Cosmoe: tells the backend which decorator to draw this app's frames with.
status_t apply_decorator(void* display);

// Hokku: reads the system colours and fonts from the settings again, for a
// program that isn't told when they change (a compositor drawing frames)
void reload_appearance();


}	// namespace BPrivate


#endif	// DECORATOR_PRIVATE_H
