# labwc-hokku on its own, for systems whose libraries are too old for labwc
# 0.20 (Ubuntu 22.04). Built apart from Hokku, so that labwc, wlroots and the
# libraries they need newer than the system's (wayland's server, libinput,
# libdrm, xkbcommon…, from subprojects/) are linked statically into
# labwc-hokku while Hokku keeps the system's:
#
#   meson setup build labwc-hokku -Ddefault_library=static && ninja -C build
#
# Where the system's are new enough, Hokku's own build has labwc-hokku (the
# labwc_hokku option) and this isn't needed.
project('labwc-hokku', 'c',
	meson_version: '>= 1.3.0',
	default_options: ['default_library=static'],
)

cc = meson.get_compiler('c')
config = configuration_data()
config.set('HAVE_PANGO', 1)
config.set('HAVE_STRCHRNUL', cc.has_function('strchrnul', prefix: '#define _GNU_SOURCE\n#include <string.h>') ? 1 : false)
config.set('HAVE_BUILTIN_CLZ', cc.has_function('__builtin_clz') ? 1 : false)
config.set('HAVE_BUILTIN_POPCOUNT', cc.has_function('__builtin_popcount') ? 1 : false)
configure_file(output: 'config.h', configuration: config)

frames_deps = [
	dependency('cairo'),
	dependency('pangocairo'),
	dependency('libpng'),
	cc.find_library('m'),
]
frames_inc = include_directories(
	'.', '../headers/libs/wayland/shared', '../headers/private/interface')
frames = static_library('hokku-frames',
	'../src/backends/wayland/shared/gonx-frame.c',
	'../src/backends/wayland/shared/haiku-frame.c',
	include_directories: frames_inc,
	dependencies: frames_deps,
	pic: true,
)
meson.override_dependency('hokku-frames', declare_dependency(
	link_with: frames,
	include_directories: frames_inc,
	dependencies: frames_deps,
))

# Ubuntu 22.04's Xwayland (22.1) has no xwayland.pc: what wlroots and labwc
# read from it, from Xwayland itself
if not dependency('xwayland', required: false).found()
	xwayland_prog = find_program('Xwayland', required: false)
	if xwayland_prog.found()
		help = run_command(xwayland_prog, '-help', check: false)
		help = help.stdout() + help.stderr()
		has = {
			'listenfd': '-listenfd',
			'no_touch_pointer_emulation': '-noTouchPointerEmulation',
			'force_xrandr_emulation': '-force-xrandr-emulation',
			'terminate_delay': '-terminate [delay]',
		}
		variables = {'xwayland': xwayland_prog.full_path()}
		foreach name, option : has
			variables += {'have_' + name: help.contains(option) ? 'true' : 'false'}
		endforeach
		# "The X.Org Foundation Xwayland Version 22.1.1 (12201001)"
		version = run_command(xwayland_prog, '-version', check: false)
		version = (version.stdout() + version.stderr()).split('Version ')[1].split()[0]
		meson.override_dependency('xwayland', declare_dependency(
			version: version,
			variables: variables,
		))
	endif
endif

subproject('labwc',
	default_options: [
		'hokku=true',
		'man-pages=disabled',
		'nls=disabled',
		'labnag=disabled',
		'systemd-session=disabled',
		'test=disabled',
		'werror=false',
	],
)
