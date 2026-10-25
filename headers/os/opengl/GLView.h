/*
 * Copyright 2008-2013, Haiku Inc. All rights reserved.
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 *
 * BGLView: an OpenGL drawing surface. Same API as Haiku's; in Hokku it renders
 * with EGL off-screen and shows each frame (SwapBuffers) through the view.
 */
#ifndef BGLVIEW_H
#define BGLVIEW_H

#include <GL/gl.h>

#define BGL_RGB			0
#define BGL_INDEX		1
#define BGL_SINGLE		0
#define BGL_DOUBLE		2
#define BGL_DIRECT		0
#define BGL_INDIRECT	4
#define BGL_ACCUM		8
#define BGL_ALPHA		16
#define BGL_DEPTH		32
#define BGL_OVERLAY		64
#define BGL_UNDERLAY	128
#define BGL_STENCIL		512

#ifdef __cplusplus

#include <View.h>

struct gl_view_private;

class BGLView : public BView {
public:
							BGLView(BRect rect, const char* name,
								ulong resizingMode, ulong mode,
								ulong options);
	virtual					~BGLView();

			void			LockGL();
			void			UnlockGL();
			void			SwapBuffers();
			void			SwapBuffers(bool vSync);

			BView*			EmbeddedView(); // deprecated, returns NULL
			void*			GetGLProcAddress(const char* procName);

			status_t		CopyPixelsOut(BPoint source, BBitmap *dest);
			status_t		CopyPixelsIn(BBitmap *source, BPoint dest);

	// Mesa's GLenum is uint, not ulong
			void			ErrorCallback(unsigned long errorCode);

	virtual	void			Draw(BRect updateRect);
	virtual	void			AttachedToWindow();
	virtual	void			AllAttached();
	virtual	void			DetachedFromWindow();
	virtual	void			AllDetached();

	virtual	void			FrameResized(float newWidth, float newHeight);

private:
							BGLView(const BGLView&);
			BGLView&		operator=(const BGLView&);

			gl_view_private* fPrivate;
			uint32			_reserved[8];
};

#endif // __cplusplus

#endif // BGLVIEW_H
