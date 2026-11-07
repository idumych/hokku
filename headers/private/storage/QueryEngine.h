/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef _QUERY_ENGINE_H
#define _QUERY_ENGINE_H


#include <SupportDefs.h>

#include <sys/types.h>


struct entry_ref;


namespace BPrivate {
namespace Storage {


/*!	A running query. BFS answers a query from its indices; a Linux file system
	has none, so this reads the attributes of the files it comes across, one
	call of GetNextRef() at a time.
*/
class QueryRun;


/*!	Starts a query of a volume.
	\return \c B_OK, \c B_BAD_VALUE for a predicate that cannot be parsed,
		\c B_NOT_SUPPORTED for a live query and on systems other than Linux.
*/
status_t	open_query(dev_t device, const char* predicate, uint32 flags,
				QueryRun** _run);
void		close_query(QueryRun* run);

/*!	The next entry that matches.
	\return \c B_OK, or \c B_ENTRY_NOT_FOUND when there are no more.
*/
status_t	next_query_ref(QueryRun* run, entry_ref* ref);
status_t	rewind_query(QueryRun* run);


}	// namespace Storage
}	// namespace BPrivate


#endif	// _QUERY_ENGINE_H
