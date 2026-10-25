/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 *
 * BGLView for Hokku. Haiku renders GL into the window through its renderer
 * add-ons; here the context is EGL (Mesa's surfaceless platform when there
 * is one, otherwise the default display) drawing into a pbuffer the size of
 * the view. SwapBuffers() reads the frame back into a BBitmap and asks the
 * window to redraw; Draw() paints it. Cosmoe only shows what a view draws on
 * its window's thread, and this way GL can run on any thread, as on Haiku.
 */

#include <GLView.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <Autolock.h>
#include <Bitmap.h>
#include <Locker.h>
#include <Window.h>

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif

typedef void (*read_pixels_func)(GLint, GLint, GLsizei, GLsizei, GLenum,
	GLenum, void*);
typedef void (*finish_func)(void);


struct gl_view_private {
	gl_view_private()
		:
		glLock("BGLView GL"),
		frameLock("BGLView frame"),
		display(EGL_NO_DISPLAY),
		context(EGL_NO_CONTEXT),
		surface(EGL_NO_SURFACE),
		surfaceWidth(0),
		surfaceHeight(0),
		width(1),
		height(1),
		front(NULL),
		pixels(NULL),
		pixelsSize(0),
		readPixels(NULL),
		finish(NULL),
		failed(false)
	{
	}

	BLocker			glLock;		// held between LockGL() and UnlockGL()
	BLocker			frameLock;	// front bitmap, view size
	uint32			options;
	EGLDisplay		display;
	EGLConfig		config;
	EGLContext		context;
	EGLSurface		surface;
	int32			surfaceWidth;
	int32			surfaceHeight;
	int32			width;		// the view's size, in pixels
	int32			height;
	BBitmap*		front;		// the last frame, drawn by Draw()
	uint8*			pixels;		// glReadPixels buffer (bottom-up)
	size_t			pixelsSize;
	read_pixels_func readPixels;
	finish_func		finish;
	bool			failed;
};


static bool
has_extension(const char* list, const char* name)
{
	if (list == NULL)
		return false;
	size_t length = strlen(name);
	for (const char* p = strstr(list, name); p != NULL; p = strstr(p + 1, name)) {
		if ((p == list || p[-1] == ' ') && (p[length] == ' ' || p[length] == '\0'))
			return true;
	}
	return false;
}


static bool
init_context(gl_view_private* p)
{
	const char* clientExtensions = eglQueryString(EGL_NO_DISPLAY,
		EGL_EXTENSIONS);
	PFNEGLGETPLATFORMDISPLAYEXTPROC getPlatformDisplay
		= (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress(
			"eglGetPlatformDisplayEXT");
	if (getPlatformDisplay != NULL
		&& has_extension(clientExtensions, "EGL_MESA_platform_surfaceless")) {
		p->display = getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
			EGL_DEFAULT_DISPLAY, NULL);
	}
	if (p->display == EGL_NO_DISPLAY)
		p->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (p->display == EGL_NO_DISPLAY
		|| !eglInitialize(p->display, NULL, NULL)) {
		fprintf(stderr, "BGLView: no EGL display\n");
		return false;
	}
	if (!eglBindAPI(EGL_OPENGL_API)) {
		fprintf(stderr, "BGLView: EGL has no desktop OpenGL\n");
		return false;
	}

	EGLint attributes[] = {
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, (p->options & BGL_ALPHA) != 0 ? 8 : 0,
		EGL_DEPTH_SIZE, (p->options & BGL_DEPTH) != 0 ? 24 : 0,
		EGL_STENCIL_SIZE, (p->options & BGL_STENCIL) != 0 ? 8 : 0,
		EGL_NONE
	};
	EGLint count = 0;
	if (!eglChooseConfig(p->display, attributes, &p->config, 1, &count)
		|| count < 1) {
		fprintf(stderr, "BGLView: no EGL pbuffer configuration\n");
		return false;
	}
	p->context = eglCreateContext(p->display, p->config, EGL_NO_CONTEXT, NULL);
	if (p->context == EGL_NO_CONTEXT) {
		fprintf(stderr, "BGLView: cannot create an OpenGL context\n");
		return false;
	}
	p->readPixels = (read_pixels_func)eglGetProcAddress("glReadPixels");
	p->finish = (finish_func)eglGetProcAddress("glFinish");
	return p->readPixels != NULL;
}


// Makes the pbuffer the view's size; call with glLock held
static bool
update_surface(gl_view_private* p)
{
	p->frameLock.Lock();
	int32 width = p->width, height = p->height;
	p->frameLock.Unlock();

	if (p->surface != EGL_NO_SURFACE && width == p->surfaceWidth
		&& height == p->surfaceHeight)
		return true;

	EGLint attributes[] = { EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE };
	EGLSurface surface = eglCreatePbufferSurface(p->display, p->config,
		attributes);
	if (surface == EGL_NO_SURFACE) {
		fprintf(stderr, "BGLView: cannot create a %dx%d pbuffer\n",
			(int)width, (int)height);
		return p->surface != EGL_NO_SURFACE;
	}
	if (p->surface != EGL_NO_SURFACE) {
		eglMakeCurrent(p->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
			EGL_NO_CONTEXT);
		eglDestroySurface(p->display, p->surface);
	}
	p->surface = surface;
	p->surfaceWidth = width;
	p->surfaceHeight = height;
	return true;
}


BGLView::BGLView(BRect rect, const char* name, ulong resizingMode, ulong mode,
	ulong options)
	:
	BView(rect, name, resizingMode, mode | B_WILL_DRAW | B_FRAME_EVENTS),
	fPrivate(new gl_view_private)
{
	fPrivate->options = options;
	fPrivate->width = rect.IntegerWidth() + 1;
	fPrivate->height = rect.IntegerHeight() + 1;
	SetViewColor(B_TRANSPARENT_COLOR);
	SetHighColor(0, 0, 0);
}


BGLView::~BGLView()
{
	gl_view_private* p = fPrivate;
	if (p->display != EGL_NO_DISPLAY) {
		eglMakeCurrent(p->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
			EGL_NO_CONTEXT);
		if (p->surface != EGL_NO_SURFACE)
			eglDestroySurface(p->display, p->surface);
		if (p->context != EGL_NO_CONTEXT)
			eglDestroyContext(p->display, p->context);
		// The display stays initialized: other views may share it
	}
	delete p->front;
	free(p->pixels);
	delete p;
}


void
BGLView::LockGL()
{
	gl_view_private* p = fPrivate;
	p->glLock.Lock();
	if (p->glLock.CountLocks() > 1)
		return;

	if (p->context == EGL_NO_CONTEXT && !p->failed && !init_context(p))
		p->failed = true;
	if (p->failed || !update_surface(p))
		return;
	if (!eglMakeCurrent(p->display, p->surface, p->surface, p->context))
		fprintf(stderr, "BGLView: eglMakeCurrent failed (0x%x)\n",
			eglGetError());
}


void
BGLView::UnlockGL()
{
	gl_view_private* p = fPrivate;
	if (p->glLock.CountLocks() == 1 && p->context != EGL_NO_CONTEXT) {
		// Let another thread make the context current
		eglMakeCurrent(p->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
			EGL_NO_CONTEXT);
	}
	p->glLock.Unlock();
}


void
BGLView::SwapBuffers()
{
	SwapBuffers(false);
}


void
BGLView::SwapBuffers(bool vSync)
{
	(void)vSync;
	gl_view_private* p = fPrivate;

	LockGL();
	if (p->failed || p->surface == EGL_NO_SURFACE) {
		UnlockGL();
		return;
	}
	int32 width = p->surfaceWidth, height = p->surfaceHeight;
	size_t rowBytes = (size_t)width * 4;
	if (p->pixelsSize < rowBytes * height) {
		free(p->pixels);
		p->pixels = (uint8*)malloc(rowBytes * height);
		p->pixelsSize = p->pixels != NULL ? rowBytes * height : 0;
	}
	if (p->pixels == NULL) {
		UnlockGL();
		return;
	}
	// B_RGB32 is BGRA in memory
	p->readPixels(0, 0, width, height, GL_BGRA, GL_UNSIGNED_BYTE, p->pixels);
	UnlockGL();

	BBitmap* frame = new BBitmap(BRect(0, 0, width - 1, height - 1), B_RGB32);
	if (frame->InitCheck() != B_OK) {
		delete frame;
		return;
	}
	// OpenGL rows go up from the bottom
	uint8* bits = (uint8*)frame->Bits();
	int32 bpr = frame->BytesPerRow();
	for (int32 y = 0; y < height; y++)
		memcpy(bits + y * bpr, p->pixels + (height - 1 - y) * rowBytes, rowBytes);

	p->frameLock.Lock();
	BBitmap* old = p->front;
	p->front = frame;
	p->frameLock.Unlock();
	delete old;

	if (LockLooperWithTimeout(100000) == B_OK) {
		Invalidate();
		UnlockLooper();
	}
}


BView*
BGLView::EmbeddedView()
{
	return NULL;
}


void*
BGLView::GetGLProcAddress(const char* procName)
{
	return (void*)eglGetProcAddress(procName);
}


status_t
BGLView::CopyPixelsOut(BPoint source, BBitmap* dest)
{
	(void)source; (void)dest;
	return B_UNSUPPORTED;
}


status_t
BGLView::CopyPixelsIn(BBitmap* source, BPoint dest)
{
	(void)source; (void)dest;
	return B_UNSUPPORTED;
}


void
BGLView::ErrorCallback(unsigned long errorCode)
{
	fprintf(stderr, "BGLView: OpenGL error 0x%lx\n", errorCode);
}


void
BGLView::Draw(BRect updateRect)
{
	BAutolock locker(fPrivate->frameLock);
	BBitmap* frame = fPrivate->front;
	if (frame == NULL) {
		FillRect(updateRect);
		return;
	}
	DrawBitmap(frame, BPoint(0, 0));
	// Not yet resized to the view: keep the rest black
	BRect b = Bounds(), f = frame->Bounds();
	if (f.right < b.right)
		FillRect(BRect(f.right + 1, b.top, b.right, b.bottom));
	if (f.bottom < b.bottom)
		FillRect(BRect(b.left, f.bottom + 1, f.right, b.bottom));
}


void
BGLView::AttachedToWindow()
{
	BView::AttachedToWindow();
	BRect b = Bounds();
	FrameResized(b.Width(), b.Height());
}


void
BGLView::AllAttached()
{
	BView::AllAttached();
}


void
BGLView::DetachedFromWindow()
{
	BView::DetachedFromWindow();
}


void
BGLView::AllDetached()
{
	BView::AllDetached();
}


void
BGLView::FrameResized(float newWidth, float newHeight)
{
	BView::FrameResized(newWidth, newHeight);
	BAutolock locker(fPrivate->frameLock);
	fPrivate->width = (int32)newWidth + 1;
	fPrivate->height = (int32)newHeight + 1;
	if (fPrivate->width < 1)
		fPrivate->width = 1;
	if (fPrivate->height < 1)
		fPrivate->height = 1;
}
