#!/bin/sh
# build-appimage.sh APP — one of Hokku's own apps (e.g. GLTeapot) as an AppImage for any
# x86_64 or aarch64 Linux desktop, built from this checkout.
#
#   sh packaging/appimage/build-appimage.sh GLTeapot
#
# Run it in CI (.github/workflows/appimage.yml, Ubuntu 24.04), not on a rolling-release
# host: the AppImage needs the oldest glibc it should run on. It builds for the machine it
# runs on (uname -m) and writes <APP>-<version>-<commit>-<arch>.AppImage in the current
# directory. Hokku is built into obj/appimage/ unless STAGE points at a DESTDIR install of
# it made with --prefix=/usr --libdir=lib.
#
# The AppImage holds the app, the Hokku libraries it links, both display backends, the
# control look, mime_db, artwork, the catalogs of libbe and the app, and the Noto fonts.
# linuxdeploy adds the rest (ICU, pango, cairo...) and leaves out what every desktop has:
# glibc, X11, fontconfig and GL/EGL, which must come from the host's drivers.
set -eu
APP=${1:?usage: build-appimage.sh APP}
cd "$(dirname "$0")/../.."
ROOT=$(pwd)
LINUXDEPLOY_TAG=1-alpha-20251107-1
APPIMAGETOOL_TAG=1.9.1
case "$(uname -m)" in
	x86_64) ARCH=x86_64 ;;
	aarch64|arm64) ARCH=aarch64 ;;
	*) echo "build-appimage: unsupported architecture $(uname -m)" >&2; exit 1 ;;
esac
VERSION=$(sed -n "s/^[[:space:]]*version: '\([^']*\)'.*/\1/p" meson.build | head -1)
OUT="$ROOT/$APP-$VERSION-$(git rev-parse --short HEAD)-$ARCH.AppImage"
WORK="$ROOT/obj/appimage"
mkdir -p "$WORK/tools"

# 1. Hokku, apps included
if [ -z "${STAGE:-}" ]; then
	STAGE="$WORK/stage"
	rm -rf "$STAGE"
	[ -d "$WORK/build" ] || meson setup "$WORK/build" --prefix=/usr --libdir=lib \
		--buildtype=release -Denable_unit_test_compile=false
	ninja -C "$WORK/build"
	DESTDIR="$STAGE" meson install -C "$WORK/build" --no-rebuild >/dev/null
fi
CROOT="$STAGE/usr"
# Apps whose dependencies are missing (GLTeapot without EGL or GL) are left out of the
# build without an error, so say so here.
[ -x "$CROOT/bin/$APP" ] || { echo "build-appimage: $APP was not built" >&2; exit 1; }
DESKTOP=$(grep -l "^Exec=$APP\b" "$CROOT"/share/applications/*.desktop | head -1)
[ -n "$DESKTOP" ] || { echo "build-appimage: no desktop entry for $APP" >&2; exit 1; }
ID=$(basename "$DESKTOP" .desktop)
ICON=$(sed -n 's/^Icon=//p' "$DESKTOP")

# 2. AppDir
APPDIR="$WORK/AppDir"
DOC="$APPDIR/usr/share/doc/$ICON"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib/addons" "$APPDIR/usr/share/cosmoe/locale/catalogs" \
	"$APPDIR/usr/share/fonts/noto" "$APPDIR/etc/fonts" "$DOC"
install -m 0755 "$CROOT/bin/$APP" "$APPDIR/usr/bin/$APP"
LIBS="libbe.so libcosmoe-x11.so libcosmoe-wayland.so"
CATALOGS="x-vnd.Haiku-libbe x-vnd.Haiku-libtextencoding $ID"
for lib in $(readelf -d "$CROOT/bin/$APP" | sed -n 's/.*NEEDED.*\[\(lib[^]]*\)\]/\1/p'); do
	case " $LIBS " in *" $lib "*) continue ;; esac
	[ -e "$CROOT/lib/$lib" ] || continue	# not Hokku's: linuxdeploy bundles it
	LIBS="$LIBS $lib"
	[ "$lib" != libtracker.so ] || CATALOGS="$CATALOGS x-vnd.Haiku-libtracker"
done
for lib in $LIBS; do
	# under the name the binary asks for, not a symlink
	install -m 0755 "$(readlink -f "$CROOT/lib/$lib")" "$APPDIR/usr/lib/$lib"
done
cp -a "$CROOT/lib/addons/control_look" "$APPDIR/usr/lib/addons/"
for d in mime_db artwork; do
	cp -a "$CROOT/share/cosmoe/$d" "$APPDIR/usr/share/cosmoe/"
done
cp -a "$CROOT"/share/cosmoe/*.png "$APPDIR/usr/share/cosmoe/"
for c in $CATALOGS; do
	[ ! -d "$CROOT/share/cosmoe/locale/catalogs/$c" ] \
		|| cp -a "$CROOT/share/cosmoe/locale/catalogs/$c" "$APPDIR/usr/share/cosmoe/locale/catalogs/"
done
for f in NotoSans-Regular NotoSans-Bold NotoSans-Italic NotoSans-BoldItalic \
		NotoSansMono-Regular NotoSansMono-Bold; do
	find "$CROOT/share/fonts" -name "$f.ttf" -exec cp {} "$APPDIR/usr/share/fonts/noto/" \;
done
cp packaging/appimage/fonts.conf "$APPDIR/etc/fonts/fonts.conf"
cp License.md "$DOC/License.md"
strip --strip-unneeded "$APPDIR/usr/bin/$APP"
find "$APPDIR/usr/lib" -name '*.so' -exec strip --strip-unneeded {} \;

GEN="$WORK/gen"
rm -rf "$GEN" && mkdir -p "$GEN"
cp "$DESKTOP" "$GEN/$ICON.desktop"
cp "$CROOT/share/icons/hicolor/256x256/apps/$ICON.png" "$GEN/$ICON.png"
cat > "$GEN/AppRun" <<EOF
#!/bin/sh
# Hokku finds its add-ons and data next to usr/lib/libbe.so; fontconfig needs telling
# about the bundled Noto fonts.
HERE=\$(dirname "\$(readlink -f "\$0")")
export FONTCONFIG_FILE="\$HERE/etc/fonts/fonts.conf"
exec "\$HERE/usr/bin/$APP" "\$@"
EOF
chmod +x "$GEN/AppRun"

# 3. Bundle the libraries Hokku and the app need. NO_STRIP: linuxdeploy's strip is
#    older than some distros' libraries.
fetch() {
	[ -x "$WORK/tools/$1" ] || { curl -fsSL "$2" -o "$WORK/tools/$1"; chmod +x "$WORK/tools/$1"; }
}
fetch linuxdeploy "https://github.com/linuxdeploy/linuxdeploy/releases/download/$LINUXDEPLOY_TAG/linuxdeploy-$ARCH.AppImage"
fetch appimagetool "https://github.com/AppImage/appimagetool/releases/download/$APPIMAGETOOL_TAG/appimagetool-$ARCH.AppImage"
export APPIMAGE_EXTRACT_AND_RUN=1	# no FUSE needed (CI)

# linuxdeploy appends $ORIGIN to a RUNPATH but keeps what is there; reset them first
# with its own patchelf so the host's ICU, pango and cairo never win.
LDROOT="$WORK/tools/linuxdeploy-root"
if [ ! -x "$LDROOT/AppRun" ]; then
	( cd "$WORK/tools" && rm -rf squashfs-root && ./linuxdeploy --appimage-extract >/dev/null \
		&& mv squashfs-root linuxdeploy-root )
fi
PATCHELF=$(find "$LDROOT" -name patchelf -type f | head -1)
for so in "$APPDIR"/usr/lib/*.so; do
	"$PATCHELF" --set-rpath '$ORIGIN' "$so"
done
DEPS_ONLY=""
for so in $(find "$APPDIR/usr/lib/addons" -name '*.so'); do
	"$PATCHELF" --set-rpath '$ORIGIN/../..' "$so"
	DEPS_ONLY="$DEPS_ONLY --deploy-deps-only=$so"
done
for so in "$APPDIR/usr/lib/libcosmoe-x11.so" "$APPDIR/usr/lib/libcosmoe-wayland.so"; do
	DEPS_ONLY="$DEPS_ONLY --deploy-deps-only=$so"
done
# shellcheck disable=SC2086
NO_STRIP=1 LD_LIBRARY_PATH="$APPDIR/usr/lib" "$LDROOT/AppRun" --appdir "$APPDIR" \
	--executable "$APPDIR/usr/bin/$APP" $DEPS_ONLY \
	--desktop-file "$GEN/$ICON.desktop" \
	--icon-file "$GEN/$ICON.png" \
	--custom-apprun "$GEN/AppRun"

# 4. Pack
rm -f "$OUT"
ARCH=$ARCH "$WORK/tools/appimagetool" --no-appstream "$APPDIR" "$OUT"
echo "Built $OUT"
