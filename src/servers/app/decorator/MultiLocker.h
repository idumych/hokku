/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Hokku: app_server's reader/writer lock as decorators know it. A decorator
 * here belongs to one window frame, so one recursive lock does for both.
 */
#ifndef MULTI_LOCKER_H
#define MULTI_LOCKER_H


#include "AppServerNames.h"

#include <Locker.h>


class MultiLocker {
public:
								MultiLocker(const char* name)
									: fLock(name) {}

			status_t			InitCheck() { return B_OK; }

			bool				IsReadLocked() const
									{ return fLock.IsLocked(); }
			bool				ReadLock() { return fLock.Lock(); }
			bool				ReadUnlock()
									{ fLock.Unlock(); return true; }

			bool				IsWriteLocked() const
									{ return fLock.IsLocked(); }
			bool				WriteLock() { return fLock.Lock(); }
			bool				WriteUnlock()
									{ fLock.Unlock(); return true; }

private:
	mutable	BLocker				fLock;
};


class AutoWriteLocker {
public:
								AutoWriteLocker(MultiLocker* lock)
									: fLock(*lock) { fLocked = fLock.WriteLock(); }
								AutoWriteLocker(MultiLocker& lock)
									: fLock(lock) { fLocked = fLock.WriteLock(); }
								~AutoWriteLocker()
									{ if (fLocked) fLock.WriteUnlock(); }

			bool				IsLocked() const { return fLocked; }
			void				Unlock()
									{ if (fLocked) fLock.WriteUnlock();
									  fLocked = false; }

private:
			MultiLocker&		fLock;
			bool				fLocked;
};


class AutoReadLocker {
public:
								AutoReadLocker(MultiLocker* lock)
									: fLock(*lock) { fLocked = fLock.ReadLock(); }
								AutoReadLocker(MultiLocker& lock)
									: fLock(lock) { fLocked = fLock.ReadLock(); }
								~AutoReadLocker()
									{ if (fLocked) fLock.ReadUnlock(); }

			bool				IsLocked() const { return fLocked; }
			void				Unlock()
									{ if (fLocked) fLock.ReadUnlock();
									  fLocked = false; }

private:
			MultiLocker&		fLock;
			bool				fLocked;
};


#endif	// MULTI_LOCKER_H
