makefile_engine_conf = configuration_data()
makefile_engine_conf.set('COSMOE_INSTALL_ROOT_DEFAULT', get_option('prefix'))

makefile_engine_installed = configure_file(
	input : 'develop/makefile-engine',
	output : 'makefile-engine',
	configuration : makefile_engine_conf,
)

install_data(
	makefile_engine_installed,
	'develop/macos-bundle.sh',
	install_dir : get_option('sysconfdir'),
)

meson.add_install_script(
	'sh',
	meson.project_source_root() / 'data/install_mime_db.sh',
	meson.project_source_root() / 'src/data/mime_db',
	meson.project_build_root() / 'rc',
	meson.project_build_root() / 'mime_db_builder',
	meson.project_build_root(),
	dir_cosmoe_data / 'mime_db',
)

# Convert shipped .catkeys into runtime .catalog files at install time.
# They are installed under <datadir>/cosmoe/locale/catalogs/<signature>/<lang>.catalog
# Without the apps, only the catalogs of the libraries and add-ons.
catalog_dirs = get_option('enable_apps') ? ['.'] : ['kits', 'add-ons']
foreach d : catalog_dirs
	meson.add_install_script(
		'sh',
		meson.project_source_root() / 'data/install_catalogs.sh',
		meson.project_source_root() / 'data/catalogs' / d,
		meson.project_build_root() / 'linkcatkeys',
		dir_cosmoe_data / 'locale' / 'catalogs',
	)
endforeach

# Required for our Wayland windowframe
install_data('artwork/icon_window.png', install_dir : dir_cosmoe_data)
install_data('artwork/sign_close.png', install_dir : dir_cosmoe_data)
install_data('artwork/sign_maximize.png', install_dir : dir_cosmoe_data)
install_data('artwork/sign_minimize.png', install_dir : dir_cosmoe_data)

install_data('artwork/cosmoe-logo.png', install_dir : dir_cosmoe_data / 'artwork')