/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Hokku: app_server's font as decorators use it, a BFont underneath.
 */
#ifndef SERVER_FONT_H
#define SERVER_FONT_H


#include "AppServerNames.h"

#include <Font.h>
#include <String.h>


class ServerFont {
public:
								ServerFont() : fFont(be_plain_font) {}
								ServerFont(const BFont& font) : fFont(font) {}
								ServerFont(const ServerFont& font)
									: fFont(font.fFont) {}
	virtual						~ServerFont() {}

			ServerFont&			operator=(const ServerFont& font)
									{ fFont = font.fFont; return *this; }
			bool				operator==(const ServerFont& other) const
									{ return fFont == other.fFont; }

			font_direction		Direction() const
									{ return fFont.Direction(); }
			uint32				Encoding() const
									{ return fFont.Encoding(); }
			uint32				Flags() const { return fFont.Flags(); }
			uint32				Spacing() const { return fFont.Spacing(); }
			float				Shear() const { return fFont.Shear(); }
			float				Rotation() const { return fFont.Rotation(); }
			float				FalseBoldWidth() const
									{ return fFont.FalseBoldWidth(); }
			float				Size() const { return fFont.Size(); }
			uint16				Face() const { return fFont.Face(); }

			const char*			Style() const;
			const char*			Family() const;

			void				SetEncoding(uint32 encoding)
									{ fFont.SetEncoding(encoding); }
			void				SetFlags(uint32 value)
									{ fFont.SetFlags(value); }
			void				SetSpacing(uint32 value)
									{ fFont.SetSpacing(value); }
			void				SetShear(float value)
									{ fFont.SetShear(value); }
			void				SetSize(float value)
									{ fFont.SetSize(value); }
			void				SetRotation(float value)
									{ fFont.SetRotation(value); }
			void				SetFalseBoldWidth(float value)
									{ fFont.SetFalseBoldWidth(value); }
			status_t			SetFace(uint16 face)
									{ fFont.SetFace(face); return B_OK; }
			status_t			SetFamilyAndStyle(const char* family,
									const char* style)
									{ return fFont.SetFamilyAndStyle(family,
										style); }

			bool				IsFixedWidth() const
									{ return fFont.IsFixed(); }
			bool				IsScalable() const { return true; }
			bool				Hinting() const { return true; }

			float				StringWidth(const char* string,
									int32 numBytes,
									const escapement_delta* delta = NULL)
										const;
			void				GetHeight(font_height& height) const
									{ fFont.GetHeight(&height); }
			void				TruncateString(BString* inOut, uint32 mode,
									float width) const
									{ fFont.TruncateString(inOut, mode,
										width); }

			const BFont&		GetBFont() const { return fFont; }

private:
			BFont				fFont;
	mutable	char				fFamily[B_FONT_FAMILY_LENGTH + 1];
	mutable	char				fStyle[B_FONT_STYLE_LENGTH + 1];
};


#endif	// SERVER_FONT_H
