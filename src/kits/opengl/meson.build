# libopengl: BGLView, rendering with EGL (see GLView.cpp)
dep_egl = dependency('egl', required: false)
if dep_egl.found() and cc.has_header('GL/gl.h')
	lib_libopengl = shared_library(
		'opengl',
		['GLView.cpp'],
		include_directories: [common_inc],
		dependencies: [dep_libbe, dep_egl],
		install_dir: dir_lib,
		install: true,
	)
	dep_libopengl = declare_dependency(
		link_with: lib_libopengl,
		include_directories: include_directories('../../../headers/os/opengl'),
	)
else
	message('libopengl (BGLView) not built: EGL or GL/gl.h missing')
endif
