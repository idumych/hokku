/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Hokku: libbe has a DesktopSettings class of its own, so app_server's goes
 * by another name in the binaries. Every app_server header decorators use
 * includes this first, so the name means the same wherever it is used, a
 * forward declaration of it included.
 */
#ifndef APP_SERVER_NAMES_H
#define APP_SERVER_NAMES_H


#define DesktopSettings ServerDesktopSettings
#define LockedDesktopSettings ServerLockedDesktopSettings


#endif	// APP_SERVER_NAMES_H
