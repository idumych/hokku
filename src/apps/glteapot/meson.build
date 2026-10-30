app_name = 'GLTeapot'
linux_desktop_id = 'x-vnd.Haiku-GLTeapot'
linux_desktop_categories = 'Graphics;3DGraphics;'

# Draws with BGLView, so only where libopengl is built (EGL and GL/gl.h found).
dep_gl = dependency('gl', required: false)
app_enabled = is_variable('dep_libopengl') and dep_gl.found() \
    and get_option('enable_libgame')
glteapot_enabled = app_enabled

app_srcs = files(
    'error.cpp',
    'FPS.cpp',
    'GLObject.cpp',
    'ObjectView.cpp',
    'TeapotApp.cpp',
    'TeapotWindow.cpp',
)

app_resource_defs = files(
    app_name + '.rdef'
)

if app_enabled
    app_link_with = [lib_libbe, lib_libgame]
    app_dependencies = [dep_libbe, dep_libgame, dep_libopengl, dep_gl]
endif
app_include_directories = common_inc
