/*
 * Copyright 2002-2008, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <OS.h>
#include <Debug.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __GLIBC__
#	include <execinfo.h>
#endif



bool _rtDebugFlag = true;


void
debugger(const char *message)
{
	debug_printf("%" B_PRId32 ": DEBUGGER: %s\n", find_thread(NULL), message);

#ifdef __GLIBC__
	// Haiku stops the team in the debugger here; the closest Linux has
	// without stopping the app is where it was called from.
	{
		void *frames[32];
		int count = backtrace(frames, 32);
		backtrace_symbols_fd(frames + 1, count - 1, 2);
	}
#endif
}


bool
_debugFlag(void)
{
	return _rtDebugFlag;
}


bool
_setDebugFlag(bool flag)
{
	bool previous = _rtDebugFlag;
	_rtDebugFlag = flag;
	return previous;
}


int
_debugPrintf(const char *format, ...)
{
	int count;
	va_list list;

	if (!_rtDebugFlag)
		return 0;

	va_start(list, format);
	count = vfprintf(stdout, format, list);
	fflush(stdout);
	va_end(list);
	return count;
}


int
_sPrintf(const char *format, ...)
{
	int count;
	va_list list;

	if (!_rtDebugFlag)
		return 0;

	va_start(list, format);
	count = debug_vprintf(format, list);
	va_end(list);
	return count;
}


int
_xdebugPrintf(const char *format, ...)
{
	int count;
	va_list list;

	va_start(list, format);
	count = vfprintf(stdout, format, list);
	fflush(stdout);
	va_end(list);
	return count;
}


int
_debuggerAssert(const char *file, int line, const char *message)
{
	char buffer[1024];
	snprintf(buffer, sizeof(buffer), "Assert failed: File: %s, Line: %d. %s",
		file, line, message);
	debugger(buffer);
	return 0;
}



// TODO: Remove. Temporary debug helper.
// (accidently these are more or less the same as _sPrintf())
int
debug_printf(const char *format, ...)
{
	int count;

	va_list list;
	va_start(list, format);

	count = debug_vprintf(format, list);

	va_end(list);
	return count;
}


int
debug_vprintf(const char *format, va_list args)
{
	// Haiku writes these to the syslog; standard error is Linux's equivalent
	return vfprintf(stderr, format, args);
}

