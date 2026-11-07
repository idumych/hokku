# libdecorator: app_server's window decorators, run in the application for
# the frames the Wayland backend draws itself. Decorator add-ons build against
# the headers installed in private/app_server, unchanged from Haiku's.
srcs_decorator = files(
	'Decorator.cpp',
	'TabDecorator.cpp',
	'DefaultDecorator.cpp',
	'RoundedDecorator.cpp',
	'WindowShape.cpp',
	'DrawingEngine.cpp',
	'ServerSupport.cpp',
	'DecorHost.cpp',
)

lib_libdecorator = shared_library(
	'decorator',
	srcs_decorator,
	include_directories: [include_directories('.'), common_inc],
	cpp_args: ['-D__HOKKU__'],
	dependencies: [dep_libbe, deps_cairo_shared],
	install_dir: dir_lib,
	install: true,
)
dep_libdecorator = declare_dependency(
	link_with: lib_libdecorator,
	include_directories: include_directories('.'),
)

install_headers(
	'BitmapDrawingEngine.h',
	'DecorManager.h',
	'Decorator.h',
	'DefaultDecorator.h',
	'AppServerNames.h',
	'Desktop.h',
	'DesktopSettings.h',
	'DrawingEngine.h',
	'DrawState.h',
	'FontManager.h',
	'MultiLocker.h',
	'PatternHandler.h',
	'RoundedDecorator.h',
	'ServerBitmap.h',
	'ServerFont.h',
	'TabDecorator.h',
	'WindowShape.h',
	subdir: 'private/app_server',
)
