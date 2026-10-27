# Hokku

Hokku is a fork of [Cosmoe](https://github.com/haydentech/cosmoe), the BeOS/Haiku API as a
C++ library for Linux (X11 and Wayland), macOS and Windows. A *hokku* is the opening verse
of a linked poem, the verse the haiku grew out of.

## Why this fork exists

- **AI-generated fixes have a home here.** Hokku incorporates fixes written with AI
  assistance. Each fix is reviewed and tested against real apps before it lands. Cosmoe
  doesn't accept AI contributions, so these fixes are kept in this fork instead of being
  proposed upstream.
- **A library to build freestanding app ports against.** The focus is narrow: a stable
  Haiku API that Renku OS and Haiku apps can be built against to run on Linux as
  standalone, freestanding programs (for example AppImages), with behaviour that matches
  Haiku as closely as possible.
- **Started in the spirit of [Renku](https://github.com/RenkuOS/Source).** Renku OS is a
  Haiku fork that welcomes machine-assisted work. Hokku brings that approach to the library
  that carries Renku and Haiku apps to other systems.

Hokku follows upstream Cosmoe and merges its changes regularly. The library names, headers
and build (`meson`, `libbe.so` and friends) are unchanged, so anything that builds against
Cosmoe builds against Hokku. Licence: MIT, as upstream (see License.md).

## Changes from Cosmoe

- **Opening a menu doesn't read the menu bar unlocked.** To avoid a deadlock on Windows, a
  menu bar unlocks its window while a menu opens, but opening the menu still read the menu
  bar's position (`Bounds()`, `ConvertToScreen()`, `ScreenLocation()`) from the tracking
  thread: a race, and a `debugger("Looper must be locked")` with a backtrace every time a
  menu opened. Those reads now take the menu bar's window lock briefly.
- **Quiet logs.** `system_beep()` wrote a terminal bell and `beep event "..." requested.` to
  standard output for every event, and the resource reader printed `read_exactly: ...` and
  `InitContainer: Caught Exception` when a file had no resources (a settings file opened
  write-only, say), which Haiku handles silently. Both are gone.
- **Notifications without a shell.** `BNotification::Send()` ran `system("notify-send \"<title>\"
  \"<content>\"")`, so a title or content with quotes or `$(...)` (a chat line, a file name
  from the network) ran commands, and the window waited for `notify-send`. Both are now
  arguments to a detached `notify-send` (`osascript` on macOS), never seen by a shell.
- **Pointer events don't walk an unlocked window.** The display thread looked up the view
  under the pointer (for every motion, click, wheel and idle event) without the window's
  lock, racing the window thread and calling `debugger("Looper must be locked")` on every
  `BView::Bounds()`. It now takes the lock only if it is free; when the window is busy, the
  window thread finds the view itself before dispatching. Waiting for the lock is still
  avoided, so a modal `BAlert::Go()` can't deadlock against the display thread.
- **`BRoster::Launch()` opens files, folders and URLs with the desktop.** Only Cosmoe apps
  could be launched, so "open file", "show in folder" and clicked links did nothing. When no
  Cosmoe app handles the document, URL (`application/x-vnd.Be.URL.*`, `text/x-email`) or the
  `be:url`/refs of the launch message, it now runs `xdg-open`, detached, without the variables
  an AppImage's AppRun set for the bundle.
- **`debugger()` and the `<Debug.h>` helpers say something.** `debug_printf()` formatted its
  text and threw it away, so `debugger()` calls (a locked-looper violation, say) were silent;
  they now go to standard error, followed by a backtrace (Haiku would stop the app in its
  debugger there). `_debuggerAssert()`, `_debugPrintf()`, `_sPrintf()` and `_setDebugFlag()`,
  which Haiku declares and old Be code calls directly, exist now.
- **`BScreen::GetBitmap()` and `ReadBitmap()`.** They were missing; on X11 they copy the
  screen into a `B_RGB32` bitmap (Wayland doesn't let clients read the screen, so there they
  return `B_UNSUPPORTED`). `BScreen::Frame()` now ends on the last pixel, as on Haiku, instead
  of one past it.
- **Node monitor code compiles unchanged.** Haiku code reads `msg->FindInt64("node", &ref.node)`
  and `FindInt32("device", &ref.device)`; on Linux `ino_t` and `dev_t` are `unsigned long`, so
  that didn't compile. `BMessage` now has those overloads. `BDirectory(const node_ref*)` and
  `SetTo(const node_ref*)` exist too: they find the directory where `watch_node()` does (a
  watch, or a `BNode`/`BEntry` the app has open).
- **`BNetworkRoute` works on Linux.** `<NetworkRoute.h>` didn't compile (glibc has no
  `route_entry`) and the class wasn't built, so apps asking for the default gateway (for
  UPnP or NAT-PMP port mapping, say) didn't link. It now reads `/proc/net/route` and
  `/proc/net/ipv6_route`; `GetDefaultRoute()`/`GetDefaultGateway()` return the default
  route with the lowest metric, as the kernel uses. `BNetworkInterface` and
  `BNetworkRoster` are still not there.
- **Smaller AppImages: a build without the apps.** `-Denable_apps=false` leaves out
  `src/apps` and `src/preferences` and installs only the catalogs of the libraries and
  add-ons, so an app bundling Hokku doesn't carry Tracker, the preferences and the rest of
  the bundled apps. The default is still to build everything.
- **`BGLView` for OpenGL.** Hokku has the OpenGL kit's `BGLView` (`libopengl.so`, header
  `<opengl/GLView.h>`, the same API as Haiku's): `LockGL()`, `UnlockGL()`, `SwapBuffers()`
  and `GetGLProcAddress()`, from any thread, as on Haiku. It renders with EGL (Mesa's
  surfaceless platform when there is one, otherwise the default display, so NVIDIA's driver
  works too) into an off-screen buffer the size of the view; `SwapBuffers()` copies the frame
  into a bitmap that the view draws, on X11 and Wayland alike. It's built when EGL and
  `GL/gl.h` are found (Debian/Ubuntu: `libegl-dev libgl-dev`). `CopyPixelsIn()`/`Out()`,
  overlays and `BGLScreen` aren't there yet. VLC Reborn's OpenGL video output uses it.
- **Wayland: menus are placed by the compositor.** Wayland doesn't tell an app where its
  windows are, and `BScreen` there covers one output, so `BMenu` moved pop-ups by a wrong
  amount trying to keep them on screen: with two monitors, a `BMenuField`'s pop-up on one of
  them opened far off to the side and was never seen. Menus now leave that to the
  compositor, which may flip, slide and resize them, horizontally as well as vertically.
- **App-wide pointer events.** Views that call `SetEventMask(B_POINTER_EVENTS)` now get every
  pointer motion, in whatever window they live, as they do on Haiku. Tooltip windows rely on
  this to close when the mouse moves; before, tooltips stayed on screen for good.
- **Font styles chosen like Haiku.** Styles lighter than Book get `B_LIGHT_FACE`, Black and
  Heavy get `B_HEAVY_FACE`, condensed widths `B_CONDENSED_FACE`, and when several styles share
  a face the canonical one wins. Before, every weight below Bold counted as regular and the
  first match was used, so `SetFamilyAndFace("Noto Serif", B_REGULAR_FACE)` drew Thin.
- **Underline and strikeout are drawn.** `B_UNDERSCORE_FACE` and `B_STRIKEOUT_FACE` stay in
  the font's face and are drawn by `DrawString()`, so `BTextView` runs show them.
- **`watch_node()` works.** It finds a path for the node (from an existing watch, or a node
  or directory the app has open, as a `BEntry` or `BNode` does) and watches that. Node
  monitor messages carry `device`/`node`/`directory` as int32/int64 like Haiku's, and
  `FindInt32()`/`FindInt64()` and `FindDevice()`/`FindInode()` each accept the other's
  fields. `name` is still a full path (see below).
- **Very short clicks no longer freeze a window.** Deleting a `BMessageRunner` right after
  creating it deadlocked its looper. `BTextView` does exactly that on a zero-length click
  (touchpad tap, xdotool, remote desktop), and the window stopped taking any input.
- **Views moved by a layout are redrawn whole.** `Invalidate()` lists a view's children for
  the update when it is called, but a layout pass could move them before the update ran: a
  child that moved into the invalid area wasn't drawn, and one that moved within it only in
  part. Windows that the window manager placed or sized differently from what they asked for
  could show half-drawn buttons and remnants of earlier frames until resized. The update now
  takes the children from their current frames.
- **A resize can't leave a window's last drawing off screen.** Resizing recreates a window's
  backing surface, dirty all over, without asking the backend to copy it; on X11 the backend's
  own redraw for the resize could run first, with the old surface. The window then kept
  showing its earlier content (a status line that had changed) until something else drew,
  such as the mouse moving over it. The window now asks for a redraw when the surface changes.
- **The text caret blinks under the backing-surface lock.** `InvertRect()`, which the caret
  uses from `Pulse()` outside any update, drew into the window's backing surface without the
  lock the backend holds while copying it to the screen, so a copy could catch the inversion
  halfway. It now holds the lock while it draws and marks the area dirty.
- **Wayland: no crash when a frameless window's view is replaced.** Destroying a window's
  root widget (a frameless window's top view, such as an alert's) left the window's main
  surface pointing at the freed widget, and a resize already queued for the window then used
  it: the app crashed right after showing the window. The surface and the window now forget
  the widget, and the places that use it check for none.
- **Leaving a window is a `B_EXITED_VIEW`.** When the pointer leaves a window (X11
  `LeaveNotify`, Wayland pointer leave), the view that was under it gets a `B_MOUSE_MOVED`
  with `B_EXITED_VIEW`, as on Haiku, and no idle event follows. Before it heard nothing, so a
  tooltip shown for it stayed on screen after the pointer had moved to another window.
  (Tested on X11; the Wayland side hasn't been tried with a real pointer yet.)
- **`B_OP_MIN` and `B_OP_MAX` compare brightness.** Like app_server, a pixel takes the
  drawing's colour only where that is darker (MIN) or brighter (MAX) as a whole, judged by
  brightness. Cairo's DARKEN and LIGHTEN, used before, compare each channel on its own, so a
  gray wash over a red icon (how many BeOS apps grey out a disabled button) gave pink.
- **Lines, rects and polygons sit on the pixels app_server uses.** Unless a view is
  `B_SUBPIXEL_PRECISE`, coordinates are rounded down to whole pixels; an even pen size is
  centred on the pixel edge, so a line of pen size 2 covers two whole rows instead of a
  blurred three; lines thicker than 1 get the butt caps they ask for; and the points of a
  filled polygon are pixel corners. Triangles, filled or not, go through pixel centers, as
  in app_server.
- **Patterns leave their low color out in `B_OP_OVER`.** A pattern's transparent pixels were
  stored with their color but no alpha, which Cairo (premultiplied) adds to what's
  underneath: a stipple filled over text painted the low color (often the 0x777477 of
  `B_TRANSPARENT_32_BIT`) between its dots. The pattern's pixels are now premultiplied.
- **8-bit and 32-bit bitmaps are transparent where Haiku's are.** Outside `B_OP_COPY`,
  `B_TRANSPARENT_MAGIC_CMAP8` pixels of a `B_CMAP8` bitmap and `B_TRANSPARENT_MAGIC_RGBA32`
  pixels of a `B_RGB32` bitmap are left out, and `B_OP_ALPHA` uses a `B_RGB32` bitmap's alpha
  bytes. Before, icons drawn with `B_OP_OVER` showed a white box where they should be clear.
- **A view bitmap's `Bits()` and its views' drawing stay in step.** On Haiku they are the same
  memory; here a `B_BITMAP_ACCEPTS_VIEWS` bitmap's views draw into a separate surface. Every
  `Bits()` call copied that surface over the bits, and `DrawBitmap()` drew the surface, so
  pixels an app wrote through `Bits()` (a fill, a pasted image, a freshly cleared canvas) were
  lost unless it locked the bitmap in between. The bitmap now tracks which copy changed: the
  bits go to the surface on `Lock()` or before the bitmap is drawn, and come back only if the
  views may have drawn. Test: `src/tests/kits/interface/bbitmap/BitmapBitsSyncTest`.
- **Translators.** `TRANSLATORS=/a:/b` no longer drops a character from every entry but the
  last. A user translator now hides the system one of the same name instead of both loading.
  If no translator loads at all, the directories searched are printed to stderr.
  `BDirectory::GetNextRef()` reports errors and fills in the parent directory's device and
  node, like `BEntry::GetRef()`.
- **`entry_ref::name` is a full path**, as in Cosmoe (Linux can't find a directory from its
  node). File panel titles and Open With show the leaf name. In app code, use
  `BPath(&ref).Leaf()` or `BEntry::GetName()`, which are portable to Haiku.
- **Installing.** The Wayland session file goes to `/usr/share/wayland-sessions` only for
  `/usr` and `/usr/local` prefixes, and inside the prefix otherwise, so non-root installs
  finish. Staged (`DESTDIR`) installs no longer delete `libbe.so` from the build machine's
  `/usr/local/lib`.

## Using it

Build it exactly like Cosmoe (below). The app templates in
[beos-app-platforms](https://github.com/idumych/beos-app-platforms) build their AppImages
(x86_64 and aarch64) against Hokku's `hokku` branch by default; an app's `haiku-app.env` can
point `COSMOE_REPO` and `COSMOE_TAG` at another fork or tag. CI caches the build by the commit
the branch points at, so a new Hokku commit is picked up on the next build.
To bundle Hokku with one app, configure it without the apps
(`meson setup builddir -Denable_apps=false`, or `meson configure builddir -Denable_apps=false`
on an existing build directory).

Apps shipped on Linux with Hokku (each also runs on Renku OS and Haiku, LinShare as HiShare):

- [Sheath](https://github.com/idumych/sheath), a SWORD Bible reader (the first).
- [Folio](https://github.com/idumych/folio), a notebook with wiki links that follow renames.
- [Gobe Productive](https://github.com/idumych/gobe-productive), the BeOS office suite
  ported from the original (a preview of its word processor). It is the largest so far,
  and found the Wayland crash listed above: an alert as an app's first window.
- [VLC Reborn](https://github.com/idumych/vlc-reborn), VLC media player with its BeOS
  interface back. It brought `BGLView` (for its OpenGL video output) and found the Wayland
  menu placement bug.
- [LinShare](https://github.com/idumych/linshare), atomozero's
  [HiShare](https://github.com/atomozero/HiShare) (the modernized BeShare file-sharing and
  chat client) for Linux. A network app with old Be code in it, it brought `BNetworkRoute`,
  screen capture, `BDirectory(node_ref)`, `Launch()` through `xdg-open` and the `<Debug.h>`
  helpers, and found the shell injection in notifications and the unlocked view lookups on
  the display thread (once `debugger()` printed).

**Problems** found with Hokku are fixed here, as new commits on `hokku` listed above, with
the smallest program that shows the bug used to test the fix. Please don't report them to
upstream Cosmoe: it doesn't take AI-assisted work, and the bug may be one Hokku introduced
or already fixed.

---

*Upstream Cosmoe's README follows.*

<p align="center">
     <img src="https://cosmoe.org/assets/img/cosmoe_desktop.png" alt="Cosmoe Logo" />
</p>

Welcome to Cosmoe!
https://www.cosmoe.org

Questions?  Suggestions?  Want to contribute?
[Join our Discord!](https://discord.gg/9ggmcsJaW)

WHAT IS COSMOE
--------------
Cosmoe is a C++ library that allows developers to build rich, easy-to-code apps using
the elegant BeOS API.  You can target Linux (both X11 & Wayland), MacOS, Windows, and
Haiku with one codebase.  When targeting MacOS, you can choose a Mac-native menubar or
Cosmoe-native in-window menus with a simple compile-time flag.

Cosmoe descends from the Haiku operating system, which itself is an open-source
re-implementation of BeOS.  Whereas Haiku is a standalone operating system, Cosmoe
is a class library that runs on all major operating systems and windowing engines.

This project is beta-level software, but is rapidly approaching maturity.
Come join in the project and make it great!


PREREQUISITES
-------------
Your system must have the following installed:

**Required:**
 - gcc or clang compilers
 - meson and ninja
 - bison and flex
 - libpng, libicu, libfreetype, libpango, libfontconfig, libglib development headers/libraries
 - For Linux: libwayland, libpixman, libxkbcommon, libxkbcommon-x11 and associated headers

**Optional, but recommended (for image format support):**
 - libjpeg (Linux) or jpeg-turbo (Mac)
 - libwebp
 - libtiff
 - libraw
 - libicns
 - If these are not installed, the jpeg, webp, tiff, raw, or icns image translators won't be available

Cosmoe has been compiled and successfully tested under the following operating systems:
 - Ubuntu 24.04 and 26.04
 - Arch Linux
 - Fedora Core 40 through 44
 - macOS 14 and 15
 - WINE 9.0
 - Windows 11

### Linux Prerequisites

On Ubuntu/Debian systems, all prerequisites can be installed with:

```bash
sudo apt install gcc g++ flex bison libpng-dev libjpeg-dev libwebp-dev libtiff-dev libraw-dev libicns-dev libicu-dev libfreetype6-dev libpango1.0-dev libpixman-1-dev libxkbcommon-dev libwayland-dev pkg-config meson libxkbcommon-x11-dev libxcursor-dev libxrandr-dev libfuse3-dev libx11-xcb-dev
```

Under Fedora/Redhat, all prerequisites can be installed with:

```bash
sudo dnf install gcc g++ flex bison libpng-devel libjpeg-devel libwebp-devel libtiff-devel libraw-devel libicns-devel libicu-devel freetype-devel pango-devel libxkbcommon-devel wayland-devel meson libxkbcommon-x11-devel libXcursor-devel libXrandr-devel fuse3-devel libX11-devel
```

Under Arch Linux, all prerequisites can be installed with:
```bash
sudo pacman -S python meson pkg-config libwebp libtiff libraw libicns gcc binutils make flex bison
```

### macOS Prerequisites

<p align="center">
     <img src="https://cosmoe.org/assets/img/cosmoe_mac.png" alt="Mac running several Cosmoe-based apps" />
</p>

On macOS, install prerequisites using [Homebrew](https://brew.sh/):

```bash
# Install Xcode Command Line Tools (if not already installed)
xcode-select --install

# Install Homebrew (if not already installed)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install build dependencies
brew install meson ninja pkg-config cairo pango libpng jpeg-turbo webp icu4c freetype fontconfig glib
```

### Windows Prerequisites

<p align="center">
     <img src="https://cosmoe.org/assets/img/CosmoeWindows.png" alt="Windows PC running several Cosmoe-based apps" />
</p>

The Windows version is cross-compiled on Linux or WSL using MinGW64 MXE, and can
run in Windows or WINE.

1. In WSL, install prerequisites and link python

```bash
sudo apt install automake autoconf libtool ruby unzip lzip gperf autopoint 7zip intltool libtool-bin python3-mako libssl-dev libpcre2-dev
sudo ln -s /usr/bin/python3 /usr/local/bin/python
```

2. Clone MXE:

```bash
git clone https://github.com/mxe/mxe.git ~/mxe
```

3. Build required MXE packages:

```bash
cd ~/mxe
make MXE_TARGETS=x86_64-w64-mingw32.shared \
     gcc cairo pango fontconfig freetype icu4c libpng jpeg libwebp
```


LINUX BUILD
-----------
Cosmoe is built and installed with a standard...
```bash
make
make install
``` 

The build objects are placed in "builddir" if you need them.

Programs are installed to `/usr/local/bin`.


MAC BUILD
---------

The Mac build and install is also performed with ```make``` and ```make install```.

Since the build requires a case-sensitive volume, ```make``` creates a suitable disk image and
does the compilation there.  You may notice this volume mounted in the Finder.

Command-line programs are installed to `/usr/local/bin` and graphical programs are installed to
`/usr/local/Applications`.  Fonts are installed to `~/Library/Fonts/Cosmoe`.


WINDOWS BUILD
-------------

The Windows build is a bit more involved.  It is cross-compiled in WSL or Linux using [MXE (M cross environment)](https://mxe.cc/).

You need an installed Linux build on the build machine first, since that provides some needed build tools.
After that you can build the Windows version:

```bash
make
make install
```

Edit Cosmoe's cross-compilation file `cross-mxe.ini` to ensure it accurately represents your MXE paths

```bash
make windows
```

On Windows, apps are not installed yet, and the exe's and dll's must be collected
into a test folder after building:
```bash
./collect-windows-binaries.sh
```

RUNNING COSMOE APPS
-------------------
Apps may be started from the commandline or double-clicked in your desktop environment.

Several sample Cosmoe apps are built by this distribution, including:
- Showcase
- AboutSystem
- Terminal
- StyledEdit
- Mandelbrot
- FontDemo
- Gradients
- Deskbar
- Clock
- Pulse
- ShowImage
- DeskCalc
- Pairs
- DriveUsage
- CharacterMap
- Icon-O-Matic
- Sudoku
- Tracker

Note that not all of them work perfectly at the moment.  I've listed them roughly
in the order of their stability and conformance to their behavior on Haiku.
Showcase (formerly Guido) is my testbed for implementing new BeOS API
functionality, so it's by far the best example of what Cosmoe can accomplish
as a UI library.

You can even try out Cosmoe as a complete desktop (as shown in the image at the
top)...  Make sure you have labwc installed, then run ./start_cosmoe_desktop.sh


PROBLEMS
--------
Cosmoe is a work in progress.  If Cosmoe fails to compile for you, or
an app crashes or displays incorrect behavior, please file an issue at gitlab.

Please see the TODO file for a list of issues and possible workarounds.


API DOCUMENTATION
-----------------
Since Cosmoe strives to conform to the Be API, the best API documentation to use is the
BeBook, available at several sites online.

https://www.haiku-os.org/legacy-docs/bebook


Acknowledgements
----------------
Thank you to the Haiku team for their work over the years that helped make this possible.
Cosmoe apps compile and run on Haiku too!

<p align="center">
     <img src="https://cosmoe.org/assets/img/ShowcaseHaiku.png" alt="Showcase app on Haiku" />
</p>