/*
 * Copyright 2004-2009, Axel Dörfler, axeld@pinc-software.de.
 * Copyright 2007, Jérôme Duval. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <Beep.h>



status_t
system_beep(const char* eventName)
{
	// Hokku: no sound for beep events yet. This used to write a BEL and
	// "beep event ... requested." to standard output for every event, which
	// a GUI app's log doesn't want.
	(void)eventName;
	return B_ERROR;
}


status_t
beep()
{
	return system_beep(NULL);
}


status_t
add_system_beep_event(const char* name, uint32 flags)
{
	(void)name;
	(void)flags;
	// ToDo: ask media server to add beep event
	return B_ERROR;
}

