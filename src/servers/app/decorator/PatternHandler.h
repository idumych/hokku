/*
 * Copyright (c) 2001-2007, Haiku, Inc.
 * Distributed under the terms of the MIT license.
 *
 * Author:	DarkWyrm <bpmagic@columbus.rr.com>
 *			Stephan Aßmus <superstippi@gmx.de>
 *
 * Hokku: app_server's Pattern; drawing with one is BView's job here.
 */
#ifndef PATTERNHANDLER_H
#define PATTERNHANDLER_H


#include <stdio.h>
#include <string.h>

#include <GraphicsDefs.h>

class BPoint;


class Pattern {
 public:

								Pattern() {} 

								Pattern(const uint64& p)
									{ fPattern.type64 = p; }

								Pattern(const int8* p)
									{ fPattern.type64 = *((const uint64*)p); }
	
								Pattern(const Pattern& src)
									{ fPattern.type64 = src.fPattern.type64; }

								Pattern(const pattern& src)
									{ fPattern.type64 = *(uint64*)src.data; }

	inline	const int8*			GetInt8() const
									{ return fPattern.type8; }

	inline	uint64				GetInt64() const
									{ return fPattern.type64; }

	inline	const ::pattern&	GetPattern() const
									{ return *(const ::pattern*)&fPattern.type64; }

	inline	void				Set(const int8* p)
									{ fPattern.type64 = *((const uint64*)p); }

	inline	void				Set(const uint64& p)
									{ fPattern.type64 = p; }

			Pattern&			operator=(const Pattern& from)
									{ fPattern.type64 = from.fPattern.type64; return *this; }

			Pattern&			operator=(const int64 &from)
									{ fPattern.type64 = from; return *this; }

			Pattern&			operator=(const pattern &from)
									{ memcpy(&fPattern.type64, &from, sizeof(pattern)); return *this; }

			bool				operator==(const Pattern& other) const
									{ return fPattern.type64 == other.fPattern.type64; }

			bool				operator==(const pattern& other) const
									{ return fPattern.type64 == *(uint64*)other.data; }

 private:

	typedef union
	{
		uint64	type64;
		int8	type8[8];
	} pattern_union;

			pattern_union		fPattern;
};

extern const Pattern kSolidHigh;
extern const Pattern kSolidLow;
extern const Pattern kMixedColors;

#endif
