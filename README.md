# Hokku

Hokku is a fork of [Cosmoe](https://github.com/haydentech/cosmoe), the BeOS/Haiku API as a
C++ library. It runs Be and Haiku apps as ordinary programs on Linux, on both X11 and Wayland,
behaving as closely to Haiku as it can. A *hokku* is the opening verse of a linked poem, the
verse the haiku grew out of.

![Hokku apps on sway with the Gonx look: Showcase's controls in front, an inactive StyledEdit window and a floating palette, on the Gonx desktop colour](docs/screenshots/gonx-theme.png)

## Why this fork exists

- **AI-assisted fixes have a home here.** Each fix is reviewed and tested against real apps
  before it lands. Cosmoe doesn't accept AI contributions, so they live in this fork.
- **A library to build freestanding app ports against.** Renku OS and Haiku apps build
  against it to run on Linux as standalone programs, such as AppImages.
- **In the spirit of [Renku](https://github.com/RenkuOS/Source)**, the Haiku fork that
  welcomes machine-assisted work.

Hokku follows upstream Cosmoe and merges its changes regularly. The library names, headers
and build are unchanged, so anything that builds against Cosmoe builds against Hokku.

## What it adds

- **Haiku apps build as they are.** Shared areas between processes, fair semaphores, a
  working node monitor, `BGLView`, `BNetworkRoute`, the public `BAboutWindow`, and
  `-lmedia`, `-ltranslation` and `-lnetwork` for unchanged Haiku Makefiles.
- **Queries and attributes on Linux.** `BQuery` works, live queries included, so Tracker's
  Find window follows the files. Attributes can be any size on file systems that allow it, and
  `watch_volume()` is there.
- **Drawing like app_server.** Pixel-exact lines and polygons, Haiku's transfer modes and
  bitmap transparency, underline and strikeout, rotated text, and fonts chosen as Haiku
  chooses them.
- **Audio through the Media Kit.** `BMediaFile` and `BSoundPlayer` play WAV, MP3, FLAC and
  Ogg Vorbis.
- **A Deskbar at home on Linux.** Apps' own Deskbar replicants, the desktop's tray icons with
  their menus, a brightness control on laptops, a menu of your Linux applications, and shared
  recent documents. In Plasma, GNOME or sway, an app's Deskbar item can sit in the desktop's
  own tray instead.
- **Notifications, the Haiku way.** Haiku's `notification_server` and preferences, also
  showing other Linux apps' notifications, with action buttons.
- **Themes for the whole desktop.** Haiku's look or [Gonx](https://github.com/idumych/gonx)
  in one step: window frame, controls, colours, fonts, wallpaper, shadows and the BeOS
  pointer, carried over to GTK 3 and Qt apps too.
- **BeOS Dano's ZSnake.** The hidden menu effect of the unreleased BeOS 5.1, where a submenu's
  selection joins the item it opened from.
- **Wayland and touch.** Client-side Haiku and Gonx frames, borderless windows that move
  themselves, menus the compositor places, and touch screens.
- **A desktop session beside others.** "Hokku" on the login screen next to Plasma or GNOME,
  without cluttering their menus. [Hokku OS](https://github.com/idumych/hokku-os) is a CachyOS
  respin with it as the desktop.

Every change, with the details and what was tested, is in
[All changes from Cosmoe](docs/changes-from-cosmoe.md).

![Deskbar's tray on sway with the Gonx look: the Gonx menu button, three tray icons and the tooltip of the mail icon](docs/screenshots/deskbar-tray-tooltip.png)

![Three notifications beside the Deskbar: a Thunderbird mail sent over D-Bus with Reply and Mark read buttons, a backup at 65% with a progress bar, and a reminder from a Hokku app with Open and Snooze buttons](docs/screenshots/notifications.png)

Haiku's look and Gonx on Hokku OS, applied to a Hokku app, a GTK 3 app and a Qt 6 app at
once:

![Haiku's look: Appearance, Network Connections and V4L2 Test Bench, each in Haiku's frame, on Haiku's blue desktop](docs/screenshots/theme-haiku.png)

![Gonx: the same three windows in the Gonx frame, on the Gonx desktop](docs/screenshots/theme-gonx.png)

ZSnake in the Deskbar, and its switch in Appearance:

| ZSnake in the Deskbar | The switch in Appearance |
|---|---|
| ![The Deskbar's menu in the Gonx look with Preferences open: the Hokku button in the selection colour, a band down the menu's edge from it to the Preferences highlight, which runs left to the submenu opened beside it, and another band down that submenu's edge to Tracker, its last item: a staircase down and to the left (shown at twice the size)](docs/screenshots/zsnake-deskbar.png) | ![Appearance's Look and feel tab: the control look, the scroll bar arrow style, and the boxes for window shadows and for joining submenus to the item they open from (ZSnake), both ticked](docs/screenshots/zsnake-appearance.png) |

<details>
<summary>More screenshots</summary>

The Deskbar: a tray icon's menu sent over D-Bus, and the Linux applications menu.

![The mail icon's menu: icons, a checkmark, a disabled entry and a submenu of radio items](docs/screenshots/deskbar-menu.png)

![The Deskbar menu open on Linux applications, with a submenu for each main category](docs/screenshots/deskbar-linux-apps.png)

The Notifications preferences:

![The Notifications preferences in the Gonx look: the General and Applications tabs](docs/screenshots/notifications-prefs.png)

The same apps before and after `gonx-theme apply`, and Gonx close-ups: tabs, controls, a
menu and an alert.

![The same desktop in Hokku's own look and in the Gonx look](docs/screenshots/gonx-before-after.png)

![Close-up of the active, inactive and floating tabs](docs/screenshots/gonx-tabs.png)

![Close-up of the Gonx controls in Showcase](docs/screenshots/gonx-controls.png)

![An open menu with the Gonx selection colour, and an alert in the Gonx border](docs/screenshots/gonx-menu-alert.png)

The Gonx controls in Hokku, GTK 3 and Qt (Kvantum):

| Hokku | GTK 3 | Qt (Kvantum) |
|---|---|---|
| ![Showcase's check boxes, radio buttons and buttons in Gonx](docs/screenshots/gonx-hokku.png) | ![GTK check boxes, radio buttons, buttons and scroll bars in Gonx](docs/screenshots/gonx-gtk.png) | ![Qt check boxes, radio buttons, buttons, scroll bar and fields in Gonx](docs/screenshots/gonx-qt.png) |

Haiku's window frame, AboutSystem, and GLTeapot through `BGLView`:

![StyledEdit, Appearance and DeskCalc in Haiku's frame on Hokku OS](docs/screenshots/haiku-decorator.png)

![AboutSystem in the Gonx look: the large Gonx badge, the system's details, and what Hokku adds](docs/screenshots/about.png)

![GLTeapot on Wayland (labwc): a lit red teapot spinning at 60 frames per second](docs/screenshots/glteapot.png)

</details>

## Apps built on Hokku

Shipped on Linux with Hokku; each also runs on Renku OS and Haiku.

- [Sheath](https://github.com/idumych/sheath), a SWORD Bible reader (the first).
- [Folio](https://github.com/idumych/folio), a notebook with wiki links that follow renames.
- [Gobe Productive](https://github.com/idumych/gobe-productive), the BeOS office suite,
  ported from the original. The largest so far.
- [VLC Reborn](https://github.com/idumych/vlc-reborn), VLC media player with its BeOS
  interface back.
- [LinShare](https://github.com/idumych/linshare), atomozero's
  [HiShare](https://github.com/atomozero/HiShare) (the modernized BeShare file-sharing and
  chat client) for Linux.
- [PowerPlay](https://github.com/idumych/powerplay), an audio player in the spirit of
  SoundPlay, with Winamp skins, a library window and a VU meter in the Deskbar or the
  desktop's tray.
- [BeSame](https://github.com/idumych/besame), a ball-removing puzzle ported from BeOS.
- [EasyPaint](https://github.com/idumych/EasyPaint), a simple bitmap editor.

Each brought fixes with it; they are listed in [All changes from Cosmoe](docs/changes-from-cosmoe.md).

## Building

Hokku has no packages or releases of its own yet, so you build it from this repository.

Its changes are made and tested on Linux. The macOS and Windows builds are upstream
Cosmoe's: their steps below are upstream's, and they aren't tried with Hokku.

### Linux

You need gcc or clang, meson and ninja, bison and flex, and the development files of
libpng, ICU, FreeType, Pango, fontconfig, GLib, Wayland, pixman, xkbcommon (with its X11
part) and cairo. libjpeg, libwebp, libtiff, libraw and libicns are optional; without them
the matching image translators aren't built. For SVG tray icons add librsvg, and for
`BGLView` EGL and the OpenGL headers.

Ubuntu and Debian:

```bash
sudo apt install gcc g++ flex bison libpng-dev libjpeg-dev libwebp-dev libtiff-dev libraw-dev libicns-dev libicu-dev libfreetype6-dev libpango1.0-dev libpixman-1-dev libxkbcommon-dev libwayland-dev pkg-config meson libxkbcommon-x11-dev libxcursor-dev libxrandr-dev libfuse3-dev libx11-xcb-dev librsvg2-dev libegl-dev libgl-dev
```

Fedora:

```bash
sudo dnf install gcc g++ flex bison libpng-devel libjpeg-devel libwebp-devel libtiff-devel libraw-devel libicns-devel libicu-devel freetype-devel pango-devel libxkbcommon-devel wayland-devel meson libxkbcommon-x11-devel libXcursor-devel libXrandr-devel fuse3-devel libX11-devel librsvg2-devel mesa-libEGL-devel mesa-libGL-devel
```

Arch Linux:

```bash
sudo pacman -S python meson pkg-config libwebp libtiff libraw libicns gcc binutils make flex bison librsvg
```

Then:

```bash
make
make install
```

The build goes into `builddir`, and programs are installed to `/usr/local/bin`.

- **Your own packages:** `make deb` and `make rpm` build them on Debian or Ubuntu and on Fedora.
- **Bundling Hokku with one app:** leave the apps out with `meson setup builddir
  -Denable_apps=false`.
- **An AppImage of one of Hokku's own apps:** `packaging/appimage/build-appimage.sh APP`.
- **Old tray icons:** `xembedsniproxy` (part of Plasma) or `snixembed` brings in XEmbed icons.
  Both are optional.

### macOS

Install the Xcode command line tools (`xcode-select --install`) and
[Homebrew](https://brew.sh/), then:

```bash
brew install meson ninja pkg-config cairo pango libpng jpeg-turbo webp icu4c freetype fontconfig glib
make
make install
```

The build needs a case-sensitive volume, so `make` creates a disk image and builds there
(you may see it mounted in the Finder). Command-line programs go to `/usr/local/bin`,
graphical programs to `/usr/local/Applications` and fonts to `~/Library/Fonts/Cosmoe`.

### Windows

Windows is cross-compiled on Linux or WSL, and runs on Windows or under Wine.

```bash
make windows-mingw
```

This downloads the MinGW-w64 toolchain and the libraries it needs, builds everything and
gathers the executables and DLLs into `win-test-mingw`. The older MXE route, `make windows`,
is still there and needs [MXE](https://mxe.cc/) set up first (`cross-mxe.ini` has its paths).

## Running the apps

Apps start from the command line or from your desktop's launcher. The build includes:

- **The desktop:** Tracker and the Deskbar.
- **Apps:** Terminal, StyledEdit, ShowImage, DeskCalc, Clock, Pulse, CharacterMap, Icon-O-Matic,
  ResEdit, DiskUsage, Chart, Columns, FontDemo, Gradients, Mandelbrot, GLTeapot (with OpenGL), Pairs,
  Sudoku, Showcase and AboutSystem.
- **Preferences:** Appearance, Backgrounds, Deskbar, Keymap, Notifications, Time and Tracker.

Not all of them behave exactly as on Haiku yet. Showcase is the testbed for new API work, and
the best example of what the library can do.

For a whole BeOS-style desktop without logging out, install labwc and run
`./start_cosmoe_desktop.sh`. To have it on the login screen, `make install` adds a Hokku session.

## Using it in your app

The API reference is the [Haiku Book](https://www.haiku-os.org/docs/api/) and the BeBook.

The app templates in [beos-app-platforms](https://github.com/idumych/beos-app-platforms)
build their AppImages (x86_64 and aarch64) against Hokku's `hokku` branch by default; an
app's `haiku-app.env` can point `COSMOE_REPO` and `COSMOE_TAG` at another fork or tag. CI
caches the build by the commit the branch points at, so a new Hokku commit is picked up on
the next build.

An app that also builds for Haiku puts Hokku-only calls (such as `BNotification`'s actions)
under `#ifdef APP_COSMOE`.

Some things work differently from Haiku. `entry_ref::name` is a full path here, so use
`BPath(&ref).Leaf()` or `BEntry::GetName()`, which work on both.

## What's missing

- **Drag and drop** from other programs doesn't work yet.
- **Printing, `BWindowStack`** and some other APIs are stubs. [TODO.md](TODO.md) has the list.
- **Media Kit:** no audio writing, no video and no Opus.
- **`BNetworkInterface` and `BNetworkRoster`** aren't there yet.
- **Queries:** `fs_open_query()` and its relatives aren't there yet, and `BQuery` is Linux only.
- **X11** doesn't get Hokku's window frames, as the window manager draws them.
- **GTK 4 and libadwaita apps** keep their own look.

## Problems

Problems found with Hokku are fixed here, as new commits on `hokku`, with the smallest
program that shows the bug used to test the fix. Please don't report them to upstream
Cosmoe: it doesn't take AI-assisted work, and the bug may be one Hokku introduced or already
fixed.

## Licence

MIT, as upstream. See [License.md](License.md).

## Thanks

To Bill Hayden and the Cosmoe contributors, who brought the Haiku API to other systems, and
to the Haiku project, whose work made both possible.
