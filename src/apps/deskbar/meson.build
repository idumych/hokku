app_name = 'Deskbar'
linux_desktop_id = 'x-vnd.Be-TSKB'
linux_desktop_categories = 'System;Utility;'

app_srcs = files(
    'BarApp.cpp',
    'BarMenuBar.cpp',
    'BarMenuTitle.cpp',
    'BarView.cpp',
    'BarWindow.cpp',
    'DeskbarMenu.cpp',
    'DeskbarUtils.cpp',
    'ExpandoMenuBar.cpp',
    'InlineScrollView.cpp',
    'LauncherView.cpp',
    'PreferencesWindow.cpp',
    'ShowHideMenuItem.cpp',
    'StatusView.cpp',
    'StatusViewShelf.cpp',
    'TeamMenu.cpp',
    'TeamMenuItem.cpp',
    'TimeView.cpp',
    'TruncatableMenuItem.cpp',
    'WindowMenu.cpp',
    'WindowMenuItem.cpp',
    'ResourceSet.cpp',
    'Switcher.cpp',
    'CalendarMenuWindow.cpp',
	'ScreenCornerSelector.cpp',
    'TrayBridgeItem.cpp',
    'BitmapMenuItem.cpp',
)

app_resource_defs = files(
    'Deskbar.rdef',
    'icons.rdef',
    'GonxLogo.rdef',
)

app_dependencies = [dep_libbe, dep_libtracker]
app_include_directories = [common_inc, tracker_inc,
    include_directories('../traybridge')]
app_cpp_args = [
    '-DDB_ADDONS',
    '-D_INCLUDES_CLASS_DEVICE_MAP=1',
    '-D_SUPPORTS_RESOURCES=1',
    '-D_SUPPORTS_FEATURE_SCRIPTING=1',
    '-D_SHOW_CALENDAR_MENU_WINDOW=1',
]

if is_linux
    # The Linux applications menu reads .desktop files with GIO and loads
    # their icons with TrayBridge's icon loader.
    deskbar_rsvg = dependency('librsvg-2.0', required: false)
    app_srcs += files('LinuxAppsMenu.cpp', '../traybridge/IconLoader.cpp')
    app_dependencies += [dependency('gio-2.0', version: '>= 2.44'),
        dependency('gio-unix-2.0'), dependency('cairo'), deskbar_rsvg]
    if deskbar_rsvg.found()
        app_cpp_args += ['-DHAVE_LIBRSVG']
    endif

    # Replicants are instantiated by looking their Instantiate() up in the
    # loaded images; the main executable's symbols must be visible for
    # TrayBridgeItem to be found.
    app_link_args += ['-Wl,--export-dynamic']
endif
