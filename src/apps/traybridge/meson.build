app_name = 'TrayBridge'
app_enabled = is_linux

app_srcs = files(
    'DBusMenu.cpp',
    'IconLoader.cpp',
    'TrayBridge.cpp',
)

app_resource_defs = files(
    'TrayBridge.rdef',
)

dep_traybridge_rsvg = dependency('librsvg-2.0', required: false)

app_dependencies = [
    dep_libbe,
    dependency('gio-2.0', version: '>= 2.44'),
    dependency('cairo'),
    dep_traybridge_rsvg,
]
app_include_directories = common_inc
app_cpp_args = dep_traybridge_rsvg.found() ? ['-DHAVE_LIBRSVG'] : []
