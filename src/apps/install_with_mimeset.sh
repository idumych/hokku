#!/bin/sh

set -eu

mimeset_path="$1"
build_root="$2"
install_dir="$3"
mime_db_dir="$4"
app_name="$5"

destdir="${DESTDIR:-}"
dest_path="${destdir}${install_dir}/${app_name}"
mime_db_path="${destdir}${mime_db_dir}"
tmpdir=

cleanup() {
    if [ -n "$tmpdir" ] && [ -d "$tmpdir" ]; then
        rm -rf "$tmpdir"
    fi
}

trap cleanup EXIT INT TERM

if [ ! -e "$dest_path" ]; then
    echo "install_with_mimeset: skipping $app_name, target not found: $dest_path" >&2
    exit 0
fi

mime_db_parent="$(dirname "$mime_db_path")"
if [ ! -d "$mime_db_path" ]; then
    mkdir -p "$mime_db_path" 2>/dev/null || true
fi

if [ ! -d "$mime_db_path" ] || [ ! -w "$mime_db_path" ]; then
    if [ ! -w "$mime_db_parent" ]; then
        echo "install_with_mimeset: skipping system MIME registration for $app_name (no write access to $mime_db_path)" >&2
        exit 0
    fi
fi

run_mimeset() {
    case "$(uname -s)" in
        Darwin)
            DYLD_LIBRARY_PATH="${build_root}${DYLD_LIBRARY_PATH:+:${DYLD_LIBRARY_PATH}}" \
                "$mimeset_path" "$@"
            ;;
        *)
            LD_LIBRARY_PATH="${build_root}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
                "$mimeset_path" "$@"
            ;;
    esac
}

# Type the app from the system MIME database being installed (installed before
# the apps), not the user's or the one already on the system: without its ELF
# executable rule, an app is typed application/octet-stream and taken for a
# document. A failure doesn't stop the install.
if ! run_mimeset -f --mimedb "$mime_db_path" "$dest_path"; then
    echo "install_with_mimeset: mimeset failed on $app_name" >&2
fi

tmpdir="$(mktemp -d "${TMPDIR:-/tmp}/cosmoe-mimeset.XXXXXX")"

# The system database under the new entries: the types the app claims are then
# copied from it, instead of created empty, which would replace the system
# entries (descriptions, sniffer rules, icons) when copied over them below.
if ! run_mimeset -a --mimedb "$tmpdir" --mimedb "$mime_db_path" "$dest_path"; then
    echo "install_with_mimeset: skipping MIME registration for $app_name" >&2
    exit 0
fi

# mimeset records where it found the app (META:PPATH, and the name it shows,
# META:S:DESC), which in a staged install is under DESTDIR: record where the
# app will be installed instead, or the app can't be found from its signature.
if [ -n "$destdir" ]; then
    installed_path="${install_dir}/${app_name}"
    find "$tmpdir" -type f | while IFS= read -r node; do
        for attr in META:PPATH META:S:DESC; do
            value="$(LD_LIBRARY_PATH="${build_root}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
                DYLD_LIBRARY_PATH="${build_root}${DYLD_LIBRARY_PATH:+:${DYLD_LIBRARY_PATH}}" \
                "$build_root/catattr" -d "$attr" "$node" 2>/dev/null)" || continue
            [ "$value" = "$dest_path" ] || continue
            LD_LIBRARY_PATH="${build_root}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
                DYLD_LIBRARY_PATH="${build_root}${DYLD_LIBRARY_PATH:+:${DYLD_LIBRARY_PATH}}" \
                "$build_root/addattr" "$attr" "$installed_path" "$node"
        done
    done
fi

mkdir -p "$mime_db_path"
cp -a "$tmpdir/." "$mime_db_path/"
find "$mime_db_path" -type d -exec chmod 755 {} \;
find "$mime_db_path" -type f -exec chmod 644 {} \;
