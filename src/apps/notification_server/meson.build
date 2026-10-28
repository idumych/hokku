app_name = 'notification_server'
app_enabled = is_linux

app_srcs = files(
    'AppGroupView.cpp',
    'AppUsage.cpp',
    'DBusNotifications.cpp',
    'NotificationReceived.cpp',
    'NotificationServer.cpp',
    'NotificationView.cpp',
    'NotificationWindow.cpp',
    'Notifications.cpp',
    '../traybridge/IconLoader.cpp',
)

app_resource_defs = files(
    'notification_server.rdef',
)

# Desktop notifications (org.freedesktop.Notifications) come in over D-Bus,
# with icons loaded as TrayBridge loads its.
notification_rsvg = dependency('librsvg-2.0', required: false)
app_dependencies = [dep_libbe, dependency('gio-2.0', version: '>= 2.44'),
    dependency('cairo'), notification_rsvg]
app_include_directories = [common_inc,
    include_directories('../../../headers/private/notification',
        '../traybridge')]
app_cpp_args = notification_rsvg.found() ? ['-DHAVE_LIBRSVG'] : []
