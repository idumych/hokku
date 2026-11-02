app_name = 'TrayExport'
app_enabled = is_linux

app_srcs = files(
    'TrayExport.cpp',
)

app_resource_defs = files(
    'TrayExport.rdef',
)

app_dependencies = [
    dep_libbe,
    dependency('gio-2.0', version: '>= 2.44'),
]
app_include_directories = common_inc
app_cpp_args = []
