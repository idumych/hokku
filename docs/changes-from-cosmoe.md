# All changes from Cosmoe

What Hokku changes or adds compared with upstream [Cosmoe](https://github.com/haydentech/cosmoe),
by area. The [README](../README.md) has the short version.

## The Deskbar and its tray

- **Apps' Deskbar replicants (remote replicants).** On Haiku the Deskbar loads an app's
  executable as an add-on to run its replicant; Linux can't load an executable into another
  process. When the Deskbar can't find a replicant's class, a stand-in
  (`RemoteReplicantView`) asks the app named by the archive's `add_on` to run it. The app
  runs the view in an offscreen window of its own (its messages and runners work as
  written) and copies what it draws into a shared area; the stand-in shows those pixels and
  forwards the pointer and wheel.
  - The replicant's popup menu is sent to the host as data (labels, marks, separators,
    submenus) and shown by the host, so it works on Wayland too; the chosen item goes back
    to the app.
  - When the app quits, the item keeps its last picture. A click starts the app again (from
    where it last ran, `/proc/<pid>/exe`, when its signature isn't installed), and its menu
    offers "Start <app>" and "Remove from Deskbar".
  - `COSMOE_DEBUG_REMOTE_REPLICANTS` traces both sides.
- **TrayExport: Deskbar items in the desktop's own tray.** Without Hokku's Deskbar (in a
  Plasma, GNOME or sway session), `BDeskbar` talks to TrayExport, starting it if needed, and
  it shows each item as StatusNotifierItems. Trays show square icons, so a wide item is cut
  into square tiles (PowerPlay's 48×16 meter: one icon per needle). Clicks, right clicks,
  middle clicks and the wheel on a tile reach the replicant where that tile is, and its menu
  is offered as a `com.canonical.dbusmenu`. Tested on X11 and headless sway, and with a test
  StatusNotifierWatcher driving it as Plasma does; not yet in a real Plasma or GNOME panel.
- **The desktop's tray icons.** On Linux the Deskbar starts `TrayBridge`
  (`src/apps/traybridge`), a StatusNotifierItem host that puts each of the desktop's tray
  icons in the tray as a replicant: KDE and Qt apps, GTK apps using libappindicator or
  libayatana-appindicator, Electron apps (Signal, Slack...), VPN clients, update notifiers.
  - A click activates the app, a middle click is its secondary action, the wheel scrolls,
    and a right click opens the app's own menu (`com.canonical.dbusmenu`) with icons,
    checkmarks, radio items, submenus and disabled entries. The app hears when the menu
    opens and closes.
  - Icon, attention, overlay, tooltip and status changes come in live. Passive icons are
    hidden, as Plasma hides them; `HOKKU_TRAY_SHOW_PASSIVE=1` shows them.
  - Icons come from the app's pixmaps or the icon theme (KDE's, GNOME's or GTK's setting,
    Breeze in an unchanged Plasma session, or `HOKKU_ICON_THEME`). SVG icons are drawn with
    librsvg when it's installed, and KDE's symbolic icons take the system colours.
  - On a desktop that already has a tray (Plasma, Xfce), TrayBridge registers with that
    desktop's StatusNotifierWatcher as one more host, so the icons are in both trays.
    Elsewhere it is the watcher.
  - Old XEmbed icons (Wine, older GTK 2 and Java apps) come through `xembedsniproxy` or
    `snixembed` when one is installed (`HOKKU_TRAY_XEMBED=0` to skip).
  - `HOKKU_TRAYBRIDGE=0` keeps it from starting. A restarted Deskbar finds TrayBridge
    running and gets its icons.
  - Tested in a Plasma 6 Wayland session, on X11 and on sway.
- **Screen brightness.** On a machine with a backlight, the tray has a brightness control
  (`src/apps/deskbar/BrightnessItem.cpp`): a click on the sun opens a slider, the wheel
  steps by 5%, and the tooltip tells the level. It sets the backlight through logind
  (`SetBrightness`, no root or udev rule needed), reads it from `/sys/class/backlight`
  preferring the firmware device as systemd does, and never goes below 1%.
- **Linux applications in the Deskbar menu.** Every `.desktop` file the desktop itself
  would show (honouring `NoDisplay`, `Hidden`, `OnlyShowIn`/`NotShowIn` and `TryExec`, your
  own overriding the system's), in submenus by freedesktop main category, with icons.
  Choosing one starts it through GIO. The list is read again when a folder changes.
- **A Gonx tab for the menu button** with the Gonx look, saying "Hokku", instead of the
  Cosmoe rocket.
- **The Deskbar menu is never empty.** It named its folder by an absolute path, so a moved
  or staged install (an AppImage) or a run from the build folder got a menu with nothing in
  it, not even About or Shutdown. A moved install now finds the menu beside
  `menu_entries`, and without one the menu shows its standard items and your own Deskbar
  folder.
- **The Deskbar drops apps that quit before it noticed their window** (the Terminal opened
  at login, say): when an app quits, it also drops listed processes that are gone.
- **Every app in the Deskbar has its icon.** When an entry gets no usable icon, the
  Deskbar fetches it again.
- **`BDeskbar::Frame()` answers.** It had been switched off because it could hang Tracker;
  it asks the Deskbar again, giving up after half a second.
- **`BDeskbar::AddItem()` returns Deskbar's error** instead of `B_NAME_NOT_FOUND`.

## Notifications

- **Haiku's `notification_server` and Notifications preferences.** The Deskbar starts it
  on Linux (`HOKKU_NOTIFICATIONS=0` leaves it off). Notifications are grouped by app beside
  the Deskbar, with icons, progress bars and the important type marked; they time out and
  update in place when sent again with the same ID.
- **`BNotification::Send()`** goes to it when it runs, and to the desktop's notifications
  (`notify-send`) when it doesn't. It used to run `system("notify-send ...")`, so quotes or
  `$(...)` in a title ran commands, and the window waited; now the text goes as arguments to
  a detached `notify-send` (`osascript` on macOS), never seen by a shell.
- **Action buttons.** A Hokku extension: `AddAction(label, message[, target])` adds a button
  that sends the message to the target (the app by default) and closes the notification;
  `CountActions()`, `ActionLabelAt()`, `ActionMessageAt()` and `ActionTargetAt()` read them.
  The class keeps its size. Renku OS has the same API
  ([RenkuOS/Source#233](https://github.com/RenkuOS/Source/pull/233)); Haiku doesn't.
- **The desktop's apps' notifications.** It serves `org.freedesktop.Notifications` when no
  other notification server does (under Plasma or GNOME their own popups keep them, until
  they stop). Icons, critical urgency, progress hints, replacement IDs, the default action,
  other actions as buttons (Reply, Mark read...) and close reasons all map across.
- **The preferences** set on/off, width, timeout and position, and mute any app; changes
  apply at once. Every app that sent one is listed, the desktop's apps too.

## Audio

- **Decoded audio is float.** Tracks said they held float but kept the file's own format,
  so 16-bit WAVs came out as noise (and NaN that broke meters). Files now decode to float;
  `DecodedFormat()` honours a requested sample format (char, uchar, short, int, float,
  double) and `ReadFrames()` converts to it.
- **Ogg Vorbis plays.** stb_vorbis 1.22 (public domain or MIT) is vendored unmodified in
  `third_party/stb_vorbis` and built into libmedia's miniaudio; `BMediaFile` couldn't open
  `.ogg` files before. Opus doesn't play yet.
- **`COSMOE_AUDIO_BACKEND=null`** plays into miniaudio's null device, paced in real time:
  for tests and headless runs that mustn't reach a sound card.
- **`BMediaRecorder` records** from the default audio input through miniaudio (Cosmoe had
  only the header). There are no media nodes: every `Connect()` opens the default capture
  device in the raw audio format asked for (mono, 16-bit, 44.1 kHz where it's a wildcard),
  and the record hook gets each 1024-frame period on miniaudio's audio thread. With
  `COSMOE_AUDIO_BACKEND=null` it records silence; `COSMOE_AUDIO_CAPTURE_FILE=<file>`
  records that file instead of the input, in real time, for tests.
- Not yet: `BMediaFile` decodes a whole file when it opens it, so a long file takes a
  moment and the memory of its decoded audio.

## Looks and themes

- **The Gonx look.** `gonx-theme apply` switches to the whole
  [Gonx](https://github.com/idumych/gonx) look and `gonx-theme revert` goes back.
  - Window frame: titled, document and floating windows get the Gonx tab, flap and rounded
    corners on Wayland, and modal windows (alerts) the Gonx border, moved by dragging it.
    The frame is client-side, so the compositor leaves its own title bar off. On X11 the
    window manager draws frames. `setdecor Gonx` and `setdecor Default` switch only the
    frame.
  - Controls: `GonxControlLook.so`, the same code as the Renku add-on; `setcontrollook`
    chooses it on its own.
  - Colours, fonts (Noto Sans at 9 and 11 points) and the desktop colour.
- **Haiku's window frame.** `setdecor Haiku` gives Hokku windows app_server's Default
  decorator on Wayland (the yellow tab, bevelled border, resize notches and a document
  window's knob), drawn client-side by app_server's own code (below), with rounded corners
  as on Renku's composited desktop. Without `libdecorator.so` it is the backends' cairo copy
  of it. `Default` stays the desktop's own frames.
- **Decorator add-ons.** Haiku's window decorators build for Hokku from unchanged sources
  and draw its frames on Wayland. `libdecorator.so` runs them in the application: Renku's
  `Decorator`, `TabDecorator`, `DefaultDecorator`, `RoundedDecorator` and `WindowShape`,
  over a thin app_server layer on libbe (a `DrawingEngine` that draws through a `BView` of
  an off-screen bitmap, `ServerFont` over `BFont`, the app's settings). Add-ons build
  against the headers in `include/private/app_server`, link `-ldecorator -lbe`, carry their
  resources in a `.cosmoe_rsrc` section, and go in `~/cosmoe/addons/decorators` or
  `<libdir>/addons/decorators`, where `setdecor`, Appearance and `DecorInfo` find them by
  name. An installed Gonx add-on takes the built-in Gonx's place. All of
  [Power Theme](https://github.com/idumych/power-theme)'s decorators build and run, and
  Power Theme itself runs on Hokku, building them there.
- **Document windows resize from the frame's knob** in the Gonx and Haiku frames; the
  content's widget used to take the pointer there.
- **Standalone apps keep the desktop's frames.** An app run from its AppImage takes the
  rest of the look but keeps the desktop's window frames (Hokku's default frame where the
  compositor draws none).
- **Themes.** Appearance has a Themes tab, and `hokku-theme` does the same from a shell
  (`list`, `apply NAME`, `save NAME`, `current`, `shadows on|off`, `corners on|off`). A
  theme sets:
  - the window decorator (Default, Haiku or Gonx) and the control look;
  - the system colours and the plain, bold, fixed and menu fonts;
  - the desktop colour and a wallpaper;
  - for other Linux apps: labwc's frames (a themerc written to
    `~/.config/labwc/themerc-override`, yours kept as `.orig`; Haiku and Gonx name their
    frame with `window.decorator`, which
    [Hokku OS's labwc](https://github.com/idumych/hokku-os/tree/main/packages/labwc-hokku)
    draws with Hokku's own frame code), the GTK, icon and cursor themes, GTK's font, and the
    Qt style (qt5ct, qt6ct, Kvantum).

  Hokku ships Haiku and Gonx in `/usr/share/cosmoe/themes`; *Save current look* keeps yours
  in `~/.config/cosmoe/themes` (a folder with a `theme.ini`, see
  `src/preferences/appearance/Theme.h`). Running apps take new colours, fonts and frames; a
  new control look shows when they restart.
- **Haiku and Gonx for GTK 3 and Qt.** GTK themes in `/usr/share/themes` and Kvantum themes
  for Qt 5 and 6 in `/usr/share/Kvantum`, generated (`data/linux/gtk/generate.py`,
  `data/linux/kvantum/generate.py`) from the control looks' own colours and shapes. The
  Hokku session hands Qt apps the theme's style and GTK's font and icons
  (`QT_STYLE_OVERRIDE`, `QT_QPA_PLATFORMTHEME=gtk3`) and keeps GTK's scroll bars beside the
  content (`GTK_OVERLAY_SCROLLING=0`). GTK 4/libadwaita apps keep their own look.
- **Window shadows and BeOS's pointer.** Windows in the Haiku and Gonx frames cast a soft
  shadow (`gonx_frame_paint_shadow()`; Hokku OS's labwc draws the same under other apps'
  windows). Frames drawn by decorators cast the shadow Renku's compositor casts, from what
  the decorator drew, or on a dark desktop (a dark panel colour) a glow round the window in
  the panel text colour, and have rounded corners where the decorator can round them
  (Appearance's *Round the corners of windows*, `hokku-theme corners`). The pointer is BeOS's hand and I-beam, as the Xcursor themes `Hokku` and
  `Hokku-Shadow` (`data/linux/cursors/generate.py`). Appearance's *Look and feel* tab turns
  window and pointer shadows on and off together. Hokku apps ask the compositor for their
  standard cursors (cursor-shape protocol), following its cursor theme.
- **ZSnake, from BeOS Dano.** Dano hid it in its Menu preferences (Ctrl+Alt+Shift showed
  "Show ZSnake!"): the selection runs as one path from a menu's item, along the open
  submenu's edge, to the submenu's selected item, a step for each level. Hokku's is a
  clean-room implementation, matched pixel for pixel against screenshots of Dano: selected
  items get Dano's highlight (the selection colour in an outline of the selected-border
  colour, corner pixels left out, a light line under it), and a submenu's window reaches 8
  pixels past the menu on each side (2 above and below), transparent there and leaving the
  pointer to what's under it, so the band runs over the supermenu's edge as Dano's does: an
  outline, 3 pixels of the selection colour, and the submenu's border as its other outline,
  every corner turned on a 3-pixel diagonal. Where Hokku's colours differ from Dano's (the
  menu border, the Gonx selection colour), the theme's are used. On Wayland the snake follows
  the submenu wherever the compositor placed it (`cosmoe_window_get_popup_offset()`); the
  strips need a compositor that blends, so X11 keeps a band drawn inside the submenu. A menu
  beside the item it opens from gets one too, so in the Deskbar the snake starts at the menu
  button, drawn in the selection colour while it's open. Submenus open on the side their menu
  opened to (the Deskbar's to the left) and slide rather than flip to fit, so the path steps
  one way and stays level with its item. Appearance's *Look and feel* tab turns it on (*Join
  submenus to the item they open from*); it's off by default, as in Dano, and menus in
  running apps show it once they restart.
- **System colours, fonts and the desktop colour are kept.** `set_ui_color()`,
  `set_ui_colors()` and `BScreen::SetDesktopColor()` did nothing, `ui_color()` always gave
  the built-in colours, and the fonts saved by Appearance were replaced by the defaults when
  an app started. They are now saved and used by apps started afterwards. A dark control
  look still brings its own colours. `_set_system_font_()`, Haiku's private call for
  Appearance and theme tools, exists. Apps no longer print `ControlLook add-on loaded
  from ...`.
- **About this system says Hokku**, with what Hokku adds, a large Gonx badge
  (`src/apps/deskbar/gonx-logo.py`), Cosmoe and Haiku credited, and the Linux kernel's
  release where the kernel's build date was.

## Drawing

- **Font styles chosen like Haiku.** Styles lighter than Book get `B_LIGHT_FACE`, Black and
  Heavy get `B_HEAVY_FACE`, condensed widths `B_CONDENSED_FACE`, and when several styles share
  a face the canonical one wins. Before, every weight below Bold counted as regular and the
  first match was used, so `SetFamilyAndFace("Noto Serif", B_REGULAR_FACE)` drew Thin.
- **`BFont::GetHeight()` keeps the fractions**, as Haiku's: ascent, descent and leading had
  come back as whole points (Liberation Serif 12: 11, 3, 0 instead of 10.69, 2.60, 0.51), so
  apps that round each laid lines out a point short.
- **Underline and strikeout are drawn.** `B_UNDERSCORE_FACE` and `B_STRIKEOUT_FACE` stay in
  the font's face and are drawn by `DrawString()`, so `BTextView` runs show them.
- **Rotated text turns about its pen location**, as in app_server. The rotation was applied
  about the view's origin, so rotated text landed away from where it was drawn (a vertical
  ruler's numbers fell outside its view).
- **Views moved by a layout are redrawn whole.** A layout pass could move children between
  `Invalidate()` and the update, leaving half-drawn buttons and remnants of earlier frames.
  The update now takes the children from their current frames.
- **A resize can't leave a window's last drawing off screen.** On X11 the backend's redraw
  for a resize could run before the new backing surface existed, so the window showed its
  earlier content until something else drew. The window now asks for a redraw when the
  surface changes.
- **The text caret blinks under the backing-surface lock.** `InvertRect()` drew without the
  lock the backend holds while copying to the screen, so a copy could catch the inversion
  halfway.
- **`B_OP_MIN` and `B_OP_MAX` compare brightness**, as app_server does. Cairo's DARKEN and
  LIGHTEN compare each channel on its own, so a gray wash over a red icon (how many BeOS
  apps grey out a disabled button) gave pink.
- **Lines, rects and polygons sit on the pixels app_server uses.** Unless a view is
  `B_SUBPIXEL_PRECISE`, coordinates are rounded down to whole pixels; an even pen size is
  centred on the pixel edge, so a line of pen size 2 covers two rows instead of a blurred
  three; thick lines get butt caps; a filled polygon's points are pixel corners, and
  triangles go through pixel centres.
- **Patterns leave their low color out in `B_OP_OVER`.** Transparent pattern pixels kept
  their color without alpha, which Cairo (premultiplied) added to what was underneath: a
  stipple over text painted the low color between its dots.
- **8-bit and 32-bit bitmaps are transparent where Haiku's are.** Outside `B_OP_COPY`,
  `B_TRANSPARENT_MAGIC_CMAP8` and `B_TRANSPARENT_MAGIC_RGBA32` pixels are left out, and
  `B_OP_ALPHA` uses a `B_RGB32` bitmap's alpha. Icons drawn with `B_OP_OVER` showed a white
  box before.
- **A view bitmap's `Bits()` and its views' drawing stay in step.** On Haiku they are the
  same memory; here they are two copies, and pixels written through `Bits()` were lost
  unless the app locked the bitmap in between. The bitmap now tracks which copy changed.
  Test: `src/tests/kits/interface/bbitmap/BitmapBitsSyncTest`.
- **`B_RGBA32` bitmaps keep their alpha when views draw into them.** Every pixel came back
  opaque, so an icon drawn on a transparent bitmap was a solid square. `B_RGB32` bitmaps
  stay opaque, as on Haiku.
- **`BWindow::Flush()` and `Sync()` put what was drawn on screen.** They did nothing, so
  drawing outside `Draw()` (an animation in a loop) waited for the next update.
- **Rect tracking's outline is on the screen.** `BeginRectTracking()` drew into the
  window, so the outline vanished once the pointer left it (a menu torn off into a floater).
  It is now drawn over everything, as app_server does: on X11 as four edge windows, on
  Wayland as a popup of the tracking window that takes no input. It also starts from where
  the pointer is now, not where it was pressed.
- **A removed view is repainted over.** Nothing redrew the area a view left when it was
  removed, so it stayed on screen; `BView::RemoveChild()` now invalidates it.

## OpenGL

- **`BGLView`** (`libopengl.so`, `<opengl/GLView.h>`, Haiku's API): `LockGL()`,
  `UnlockGL()`, `SwapBuffers()` and `GetGLProcAddress()` from any thread. It renders with
  EGL (Mesa's surfaceless platform when there is one, otherwise the default display, so
  NVIDIA's driver works too) into an off-screen buffer that the view draws, on X11 and
  Wayland alike. It's built when EGL and `GL/gl.h` are found. `DirectConnected()` and
  `EnableDirectMode()` exist and do nothing. `CopyPixelsOut()` copies the last frame
  into a `B_RGB32` or `B_RGBA32` bitmap, as Haiku's renderers do; `CopyPixelsIn()`,
  overlays and `BGLScreen` aren't there yet. `SwapBuffers()` posts the frame to the window rather than
  locking it, so pointer motion no longer drops GLTeapot from about 700 frames per second
  to 9-80.
- **GLTeapot**, Haiku's OpenGL demo, is included (Deskbar: Demos).
- **`BScreen::WaitForRetrace()`** waits for the next tick of a 60 Hz clock, since the
  display server doesn't tell apps when the screen refreshes.

## Queries

- **`BQuery` works** (it was a stub; every call failed). Haiku's own parser and matching
  decide what a predicate means: wildcards, numbers compared as numbers, `&&`, `||`, `!`,
  `name`, `size`, `last_modified` and `%dates%`. 95 of 98 predicates, run on the same files,
  gave the same result as BFS on R1/beta6; of the rest, two were attributes without an index
  there, and Hokku refuses an unclosed `(` that beta6 accepted. `BVolume::KnowsQuery()` and
  `KnowsAttr()` are true on a file system with extended attributes.
- **What a query looks at.** On the home folder's volume, the settings folder and the home
  folder; on the root volume nothing; on any other volume all of it. Hidden entries are left
  out, links aren't followed, and no file is opened, so nothing watching a file sees it as
  changed.
- **A list stands in for the indices.** Few files have attributes (70,000 of 4.5 million in the
  home folder it was measured on), so a predicate that can only match a file with an attribute
  looks at those files alone: 0.17 s there, instead of 9 s for walking. The list is kept in
  `~/.cache/cosmoe/query-index`. It has no values, so what a query says of a file is what the
  file says now. It comes from a walk, and `BNode::WriteAttr()` adds to it in every program
  (`fs_write_attr()` called directly doesn't, as in `addattr`).
  What it can lack is a file that got its attributes elsewhere (`setfattr`, `cp -a`, a restore)
  or was moved by another program, until the next walk: when a query finds the list old (10
  minutes, or a hundred times what a walk takes) or an entry gone (1 minute, or thirty times),
  one is made in the background, by one program at a time. For an exact answer, set
  `B_QUERY_NON_INDEXED`: it looks at every file. A predicate that can match without an attribute,
  like `name == "x"` or anything `||` it, always walks: about 2 µs a file.
- **Live queries.** With `SetTarget()`, a query goes on sending `B_QUERY_UPDATE`, so Tracker's
  Find window follows the files. `Fetch()` doesn't wait: the first walk is on the query's own
  thread. The same changes on BFS and here gave the same messages, but for the rename of a
  folder with a match in it: BFS says nothing, here the path changes and so does the entry.
  A home folder can have more folders than a user has inotify watches, so a query watches
  4096 of them: where it started, where it has a match, then the nearest. A change there
  comes at once; one anywhere else comes when the query looks again, once a minute, at the
  list if it can use it. If it has to walk, that's at a low priority and about 3% of one CPU
  (4 minutes for 4.5 million files). `B_QUERY_WATCH_ALL` adds nothing yet: a move is a removal and a creation, and a
  change that doesn't affect the match isn't reported.
- **Every index exists, so far as a program can tell.** There is nothing to keep up, and
  anything an index is for can be asked: `fs_stat_index()` succeeds and `fs_create_index()` says
  `B_FILE_EXISTS` on a volume with queries. `BRoster` explicitly doesn't use queries to look for
  an app, which would be no way to start one.
- **`watch_volume()`** was declared but not there. It watches the folders that a query starts
  from and up to 4096 nearest, as they are when it's called: a folder made later isn't covered,
  nor is one too deep. Its messages, like those of the node monitor and of live queries, come
  with no reply address, as from the kernel on Haiku: a handler that answers every message would
  otherwise answer itself for ever.
- **Linux only.** `fs_open_query()` and its relatives aren't there yet.

## Attributes

- **A value can be any size.** BFS keeps any size, but btrfs took about 10 KB of one value. A
  value that doesn't fit is split into hidden `COSMOE:MORE:<n>:<name>` attributes, 4 KB each, that
  reads, `GetAttrInfo()`, listing, renaming and removing all treat as one value; a value that fits
  stays one, for other programs. If a value can't be written, the old one stays.
- **Attributes that won't fit on the file at all** go to a store of the user's, in
  `~/.local/share/cosmoe/attr-overflow`: ext4 keeps about a block of attributes for a file, all
  together, unless it was made with `ea_inode`. The largest values go first, so small ones stay on
  the file. The API, listing and queries treat them as any other. They are found by the file's
  file system, inode and birth time: they stay with a file that is renamed or moved inside its file
  system, a later file with the same inode can't get them, and a file system with no birth time
  can't use the store. A copy made by another program (`cp -a`) doesn't get them, and neither does
  another user. What was left by a file that was deleted isn't cleaned up yet.
- **A failed write says why:** `B_DEVICE_FULL`, `B_NAME_TOO_LONG`, `B_PERMISSION_DENIED`,
  `B_READ_ONLY_DEVICE` or `B_NOT_SUPPORTED`, instead of `B_BAD_VALUE` for everything.
- **A read can be of part of a value,** with an offset or a smaller buffer, as on Haiku. It used
  to ignore the offset and fail on a small buffer.
- **`resattr`** is there.

## Windows, menus and input

- **New windows open at their frame's position**, as app_server places them. They were
  left where the window manager put them (a pop-up slider meant for its control came up
  in the screen's corner). On X11 the position is the content's, as on Haiku
  (`StaticGravity`); window managers put the title bar there, so windows landed a title
  bar low and centred ones sat off centre.
- **Popup windows.** A `kMenuWindowFeel` window with `kWindowPopupGrabFlag`, made a popup
  of its parent with `BPrivate::set_popup_window_parent()` (`WindowPrivate.h`), opens
  beside the parent, takes the pointer and keyboard (an `xdg_popup` grab on Wayland), and
  is sent `B_QUIT_REQUESTED` when the user clicks elsewhere. On X11 it isn't grabbed.
- **Pop-up menus stay open after the press that opened them**, and Escape or a click
  outside closes them. A `BPopUpMenu::Go()` on the window's own thread blocks that window,
  so its press, release and Escape never reached the menu; they are now counted as they
  arrive from the backend. A right click's context menu no longer chooses the first item
  the pointer crosses, and a menu closed by Escape or a click elsewhere chooses nothing
  (it chose the item last under the pointer).
- **X11: a deactivated pop-up stays up.** Menus are deactivated as they open, which lowered
  them under the window they pop up from.
- **X11: pop-up menus without a parent window open** (`BPopUpMenu::Go()` from a window
  that isn't on screen, such as a replicant's).
- **Legacy cursors look as on Haiku.** In BeOS cursor data the hot spot is y, then x, and a
  pixel with only its image bit set is black; Hokku had the hot spot swapped and left such
  pixels out, so some cursors pointed off or didn't show.
- **`BClipboard` keeps the whole message.** The system clipboard carries only text, so
  other fields were lost, and a commit without text left the clipboard to whoever had it.
  A commit now always takes the system clipboard and `Lock()` gives the message back while
  the system clipboard still holds its text.
- **`GetMouse()` reports the buttons as they are now**, even while the window is busy: a
  view tracking the mouse in `MouseDown()` never saw the button come up and looped forever.
  Presses and releases also update the position it falls back on, so the first click after
  entering a window counts where it happened.
- **App-wide pointer events.** Views that call `SetEventMask(B_POINTER_EVENTS)` get every
  pointer motion, in whatever window they live, as on Haiku. Tooltips rely on this to close
  when the mouse moves; before, they stayed on screen for good. Events go to the listening
  view itself, without holding a lock while sending, and a busy window drops them rather
  than blocking the display thread.
- **Leaving a window is a `B_EXITED_VIEW`**, so a tooltip shown for a view doesn't stay
  after the pointer moves to another window.
- **Pointer events don't walk an unlocked window.** The display thread looked up the view
  under the pointer without the window's lock. It now takes the lock only if it is free;
  otherwise the window thread finds the view itself. A modal `BAlert::Go()` can't deadlock
  against it.
- **Opening a menu doesn't read the menu bar unlocked**, and a menu that fills slowly (the
  Deskbar's, on a cold machine) locks its parent before asking where the mouse is. Both
  printed "Looper must be locked".
- **Shortcut characters fit in menus.** Wide ones (W, M) were cut off at the right edge.
- **Very short clicks no longer freeze a window.** Deleting a `BMessageRunner` right after
  creating it deadlocked its looper, and `BTextView` does exactly that on a zero-length
  click (touchpad tap, xdotool, remote desktop).
- **A keymap without a configured layout.** With no layout set and no `setxkbmap` (a
  Wayland-only system), `get_key_map()` failed and Tracker crashed on startup. It builds
  the keymap from xkbcommon's defaults (`XKB_DEFAULT_*`, else us).

## Wayland and touch

- **Touch screens work.** The first finger on a window drives the mouse, as a tablet does
  on Haiku: a tap is a left click, a moving finger drags, and one held still for 0.6
  seconds is a right click. Dragged from a menu into a submenu, it reaches the submenu where
  the compositor really put it; a mouse drag does the same. Tapping or releasing on an item
  with a submenu opens the submenu, as on Haiku. `HOKKU_TOUCH_DEBUG=1` prints what touch and
  the pointer do. Tested with KWin's fake input and on a touch screen in Plasma.
- **Borderless windows.** `B_NO_BORDER_WINDOW_LOOK` windows (a skinned player, a splash
  screen) got the compositor's title bar and the 200×160 minimum size of framed windows.
  They now have no frame and keep their size, and an app moving one with `MoveTo()` while
  a button is down starts the compositor's interactive move, since Wayland apps can't place
  windows.
- **Menus are placed by the compositor**, which may flip, slide and resize them. `BMenu`'s
  own placement, with no real window positions, put a `BMenuField`'s pop-up off screen on a
  second monitor.
- **A panel can sit away from its corner.** A layer-shell surface with the private
  `kWindowPanelOffsetFlag` turns `MoveTo()` into margins from its corner, so the
  notification window sits beside the Deskbar. The margins follow the panel as it grows.
- **Panels have no window frame**, so the notifications aren't cut off or framed.
- **Window frames in an AppImage.** The frame's images are found in the data folder next to
  libbe, so a relocated copy draws its client-side frame.
- **The desktop and the Deskbar come back after a text console.** labwc removes the outputs
  while a text console has the screen, and layer surfaces went with them. They are made
  again when an output appears, including for a Deskbar started meanwhile, and
  `BScreen::Frame()` is the last screen rather than empty, so Tracker no longer piles the
  desktop icons at 0,0.
- **No crash when a frameless window's view is replaced.** The window's surface kept
  pointing at the freed root widget (an alert's top view) and a queued resize used it.

## Desktop integration

- **Hokku next to another desktop.** Installed beside Plasma, GNOME or another desktop,
  Hokku is one more choice on the login screen. The other desktop's menus don't list
  Hokku's own desktop parts (`OnlyShowIn=cosmoe`), while Hokku's apps stay listed and claim
  no file types. The session takes the system keyboard layout (from `localectl`,
  `/etc/default/keyboard` or `/etc/vconsole.conf`), and `cosmoe-portals.conf` picks portal
  backends: wlroots' for screenshots and screen sharing, GTK's or KDE's for the rest.
- **The desktop session as Hokku OS has it.** `labwc-hokku`, built with Hokku
  (`subprojects/labwc.wrap`, option `labwc_hokku`), draws every other app's window in the
  frame Hokku's own windows have: the decorator Hokku's settings choose, add-ons included,
  through `libdecorator`, with rounded corners and a shadow or glow, following Appearance,
  Power Theme and `setdecor` as they change them (`window.decorator: Hokku`). The session
  brings labwc's settings and theme of its own (`share/cosmoe-desktop`, in
  `XDG_CONFIG_DIRS` and `XDG_DATA_DIRS` only for it), kanshi, `hokku-session.target` for the
  desktop's autostart applets, KMix in the tray, cleanup after a session that was killed, and
  the Gonx look with ZSnake for a new user (`HOKKU_FIRST_LOOK`). It falls back to plain labwc
  where labwc-hokku couldn't be built (Ubuntu 24.04's libraries are too old for labwc 0.20).
- **The desktop session on Arch, and on real GPUs.** `cosmoe-desktop.sh` builds the first
  login's MIME database from pacman packages too, not only dpkg and rpm, and from their
  files only (scanning `/usr` took minutes), layered over the system database. It finds
  polkit-gnome's agent on Arch and Fedora, starts mako only when Hokku's notifications are
  off, and uses the pixman renderer and software cursors only under VirtualBox.
- **`BRoster::Launch()` opens files, folders and URLs with the desktop.** When no Cosmoe app
  handles a document, URL or `be:url`, it runs `xdg-open`, detached, without the variables
  an AppImage's AppRun set.
- **Launched apps aren't left as zombies.** Apps are started detached, so nothing has to
  wait for them, as on Haiku.
- **Apps without windows are seen.** `BRoster::GetAppInfo()`, `IsRunning()` and
  `GetAppList()` find running apps that have no window, and `BMessenger::IsValid()` is true
  for a messenger into another running app, so `hey` reaches the Deskbar, Tracker's desktop
  and the servers (`hey Deskbar BeMn` opens the Deskbar menu). `GetAppList()` lists each app
  once, by its real team.
- **`BRoster::FindApp()` finds running apps wherever they are.** Haiku finds an app
  anywhere by a query on its signature; without queries, an app run from a build folder or
  installed without a `.desktop` file wasn't found, so `BAboutWindow` showed no version or
  icon for it. When the MIME database and `.desktop` files don't know the signature, it
  takes the running app's executable from `/proc/<pid>/exe`.
- **Recent documents, folders and apps are shared**, as Haiku's registrar keeps them, in
  `~/.config/cosmoe/roster/recent_documents`, `recent_folders` and `recent_apps` (one
  `signature<TAB>path` per line, newest first), and survive a restart. Background apps,
  Tracker, the Deskbar and command line tools aren't recent apps.
- **`BShelf` works again.** Cosmoe had replaced it with stubs (`AddReplicant()` always
  failed), likely because `<AutoLock.h>` collides with `<Autolock.h>` on macOS's
  case-insensitive file system. Haiku's implementation is back, using `<AutoLocker.h>`.
- **`BScreen::GetBitmap()` and `ReadBitmap()`** copy the screen into a `B_RGB32` bitmap on
  X11 (Wayland doesn't let clients read the screen: `B_UNSUPPORTED`). `BScreen::Frame()`
  ends on the last pixel, as on Haiku. `BScreen::GetMode()` gives the screen's size with a
  60 Hz timing, so apps can work out the refresh rate.

## Kernel, files, storage and network

- **Areas are shared between processes on Linux.** Each area is a POSIX shared memory
  object in a registry all of a user's processes share (`/hokku-areas-<uid>`). As on Haiku,
  area IDs are unique across teams, `find_area()` and `get_area_info()` see every team's
  areas, `clone_area()` maps the same pages, protections are enforced, and a team's areas
  are freed when it exits, or by the next team after a crash. `_kern_transfer_area()`
  returns the area's ID, so `BMessage`'s pass-by-area works. macOS and Windows keep
  per-process areas. Test: `area_shared_test`.
- **Semaphores are fair, as on Haiku.** POSIX semaphores don't queue their waiters, so a
  looper releasing and reacquiring its lock between messages starved other threads (up to
  81 ms, 0.3 ms now). Each semaphore is now a count and a queue. Also: `B_ABSOLUTE_TIMEOUT`
  isn't read as wall-clock time, `delete_sem()` wakes its waiters, `B_RELEASE_ALL` and
  `B_RELEASE_IF_WAITING_ONLY` work, and a killed thread leaves the queue.
- **Every thread has its own ID.** `find_thread(NULL)` gave `B_NAME_NOT_FOUND` for threads
  not started by `spawn_thread()` (a `BMessageRunner`'s), so they shared one ID and a
  `BLocker` took one's lock as another's. A window's message queue broke and the Terminal
  stopped showing what was typed.
- **`watch_node()` works.** It finds a path for the node (an existing watch, or a node the
  app has open) and watches that. Node monitor messages carry `device`/`node`/`directory`
  as int32/int64 like Haiku's, and either getter reads either field.
- **Node monitor code compiles unchanged.** `BMessage` has `FindInt64("node", &ref.node)`
  and `FindInt32("device", &ref.device)` overloads for Linux's `ino_t` and `dev_t`, and
  `BDirectory(const node_ref*)` and `SetTo(const node_ref*)` exist.
- **Opening a file is not a change.** Closing a file without writing sent
  `B_STAT_CHANGED`, so StyledEdit said files "were modified by another application".
- **Closing a directory no longer leaks a descriptor.** One leaked per `BDirectory` or
  `BEntry`; the Deskbar leaked about ten each time its menu opened, until its menu came up
  empty. `BDirectory` also no longer closes its descriptor twice.
- **`entry_ref::name` is a full path**, as in Cosmoe (Linux can't find a directory from its
  node). File panel titles and Open With show the leaf name. In app code, use
  `BPath(&ref).Leaf()` or `BEntry::GetName()`, which are portable to Haiku.
- **Translators.** `TRANSLATORS=/a:/b` no longer drops a character from each entry; a user
  translator hides the system one of the same name; if none load, the directories searched
  are printed. `BDirectory::GetNextRef()` reports errors and fills in the parent's device
  and node.
- **`BNetworkRoute` works on Linux**, reading `/proc/net/route` and `/proc/net/ipv6_route`;
  `GetDefaultRoute()`/`GetDefaultGateway()` return the default route with the lowest
  metric. `BNetworkInterface` and `BNetworkRoster` aren't there yet.

## Logs and debugging

- **Quiet logs.** `system_beep()` no longer writes a bell and `beep event ...` to standard
  output, and the resource reader no longer prints `read_exactly: ...` and `InitContainer:
  Caught Exception` for files without resources.
- **`debugger()` and the `<Debug.h>` helpers say something.** `debugger()` messages go to
  standard error with a backtrace (Haiku would stop the app in its debugger).
  `_debuggerAssert()`, `_debugPrintf()`, `_sPrintf()` and `_setDebugFlag()`, which old Be
  code calls directly, exist.

## Build and install

- **Haiku Makefiles link unchanged.** `libmedia`, `libtranslation` and `libnetwork` are
  installed as linker scripts that link libbe, where those kits live here.
- **`BAboutWindow` is public**, in `headers/os/interface` and `InterfaceKit.h`, as in Renku;
  `<private/interface/AboutWindow.h>` still works.
- **Smaller AppImages: a build without the apps.** `-Denable_apps=false` leaves out
  `src/apps` and `src/preferences`, so an app bundling Hokku doesn't carry Tracker and the
  rest.
- **AppImages of Hokku's own apps.** `packaging/appimage/build-appimage.sh APP` packs an app
  with what it links, both display backends, its catalog and the Noto fonts. CI builds
  GLTeapot this way for x86_64 and aarch64 and starts it under X11 and Wayland.
- **Installing.** The Wayland session file goes inside the prefix unless it is `/usr` or
  `/usr/local`, so non-root installs finish. Staged (`DESTDIR`) installs no longer delete
  `libbe.so` from the build machine, and apps' MIME entries record where they're installed,
  not their staging path.
- **Installed apps are typed as apps.** The system MIME database is installed before the
  apps, so `mimeset` no longer types them `application/octet-stream` in a package build.
- **Installs and types apps on ext4.** ext4 keeps at most 4 KiB of attributes per file, and
  `mimeset` failed on Tracker, Chart and Showcase, stopping `meson install` on Ubuntu and
  Debian. An attribute that doesn't fit is now left out; `BAppFileInfo` reads it from the
  resources instead.
- **CI runs the kernel and unit tests.** The kernel tests had all been skipped, since the
  downloaded build lost its executable bits.
