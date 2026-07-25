#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "Usage: $0 <build-directory> <output-zip>" >&2
    exit 2
fi

build_dir="$(realpath "$1")"
case "$2" in
    /*) output_zip="$2" ;;
    *) output_zip="$(pwd)/$2" ;;
esac

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
stage_root="${build_dir}/linux-portable-stage"
package_root="${stage_root}/MdsScope"
lib_dir="${package_root}/lib"
plugin_dest="${package_root}/plugins"
gio_module_dir="${package_root}/gio-modules"

cmake -E remove_directory "${stage_root}"
cmake -E make_directory "${package_root}" "${gio_module_dir}"
cmake --install "${build_dir}" --prefix "${package_root}"

cmake -E copy "${project_root}/packaging/linux/portable-MdsScope" "${package_root}/MdsScope"
cmake -E copy "${project_root}/packaging/linux/portable-transfer" "${package_root}/transfer"
cmake -E copy "${project_root}/packaging/linux/qt.conf" "${package_root}/bin/qt.conf"
cmake -E copy "${project_root}/README.md" "${package_root}/README.md"
cmake -E copy "${project_root}/COPYING" "${package_root}/COPYING"
chmod +x "${package_root}/MdsScope" "${package_root}/transfer"

if command -v qtpaths >/dev/null 2>&1; then
    qtpaths_command=qtpaths
elif command -v qtpaths6 >/dev/null 2>&1; then
    qtpaths_command=qtpaths6
else
    echo "Could not find qtpaths or qtpaths6" >&2
    exit 1
fi

qt_plugins="$("${qtpaths_command}" --query QT_INSTALL_PLUGINS)"
plugin_groups=(
    egldeviceintegrations
    generic
    iconengines
    imageformats
    networkinformation
    platforminputcontexts
    platforms
    platformthemes
    styles
    tls
    wayland-decoration-client
    wayland-graphics-integration-client
    wayland-shell-integration
    xcbglintegrations
)

cmake -E make_directory "${plugin_dest}"
for group in "${plugin_groups[@]}"; do
    if [[ -d "${qt_plugins}/${group}" ]]; then
        cmake -E copy_directory "${qt_plugins}/${group}" "${plugin_dest}/${group}"
    fi
done

if [[ ! -f "${plugin_dest}/platforms/libqxcb.so" ]]; then
    echo "Qt XCB platform plugin was not found in ${qt_plugins}" >&2
    exit 1
fi

mapfile -d '' plugin_files < <(find "${plugin_dest}" -type f -name '*.so' -print0)
dependency_queue=(
    "${package_root}/bin/MdsScope"
    "${package_root}/bin/transfer"
    "${plugin_files[@]}"
)

cmake -E make_directory "${lib_dir}"
declare -A copied_dependencies=()

is_system_runtime()
{
    case "$1" in
        ld-linux*.so* | libc.so* | libdl.so* | libm.so* | libpthread.so* | \
        libresolv.so* | librt.so* | libutil.so* | libanl.so* | libnss_*.so*)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

queue_index=0
while ((queue_index < ${#dependency_queue[@]})); do
    binary="${dependency_queue[queue_index]}"
    ((queue_index += 1))

    if ldd "${binary}" | grep -q 'not found'; then
        ldd "${binary}" >&2
        echo "Unresolved dependency in ${binary}" >&2
        exit 1
    fi

    while IFS= read -r dependency; do
        [[ -n "${dependency}" ]] || continue
        dependency_name="$(basename "${dependency}")"
        if is_system_runtime "${dependency_name}" || [[ -n "${copied_dependencies[${dependency_name}]:-}" ]]; then
            continue
        fi

        copied_dependencies["${dependency_name}"]=1
        cp -L "${dependency}" "${lib_dir}/${dependency_name}"
        dependency_queue+=("${lib_dir}/${dependency_name}")
    done < <(
        ldd "${binary}" |
            awk '/=> \// { print $3 } /^[[:space:]]*\/.*\(0x[0-9a-f]+\)/ { print $1 }'
    )
done

if LD_LIBRARY_PATH="${lib_dir}" ldd "${package_root}/bin/MdsScope" | grep -q 'not found'; then
    LD_LIBRARY_PATH="${lib_dir}" ldd "${package_root}/bin/MdsScope" >&2
    echo "Portable MdsScope package still has unresolved dependencies" >&2
    exit 1
fi

cmake -E rm -f "${output_zip}"
(
    cd "${stage_root}"
    zip -q -r -9 "${output_zip}" MdsScope
)

echo "Created ${output_zip}"
