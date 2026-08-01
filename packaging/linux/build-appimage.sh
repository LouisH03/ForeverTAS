#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build_dir="${FOREVERTAS_BUILD_DIR:-${repo_root}/build/package-linux}"
dist_dir="${FOREVERTAS_DIST_DIR:-${repo_root}/dist}"
tools_dir="${FOREVERTAS_TOOLS_DIR:-${repo_root}/build/package-tools}"
appdir="${FOREVERTAS_APPDIR:-${build_dir}/AppDir}"

case "$(uname -m)" in
    x86_64|amd64) appimage_arch="x86_64" ;;
    aarch64|arm64) appimage_arch="aarch64" ;;
    *)
        echo "Unsupported AppImage architecture: $(uname -m)" >&2
        exit 2
        ;;
esac

mkdir -p "${dist_dir}" "${tools_dir}"

linuxdeploy="${LINUXDEPLOY:-${tools_dir}/linuxdeploy-${appimage_arch}.AppImage}"
qt_plugin="${LINUXDEPLOY_PLUGIN_QT:-${tools_dir}/linuxdeploy-plugin-qt-${appimage_arch}.AppImage}"

download_tool() {
    local destination="$1"
    local url="$2"
    if [[ ! -x "${destination}" ]]; then
        echo "Downloading $(basename "${destination}")"
        curl --fail --location --retry 3 --output "${destination}" "${url}"
        chmod +x "${destination}"
    fi
}

download_tool "${linuxdeploy}" \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${appimage_arch}.AppImage"
download_tool "${qt_plugin}" \
    "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-${appimage_arch}.AppImage"

cmake_args=(
    -S "${repo_root}"
    -B "${build_dir}"
    -G Ninja
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_INSTALL_PREFIX=/usr
    -DBUILD_TESTING=OFF
)
if [[ -n "${FOREVERTAS_ENABLE_CUDA:-}" ]]; then
    cmake_args+=(
        "-DFOREVERTAS_ENABLE_CUDA=${FOREVERTAS_ENABLE_CUDA}"
    )
fi
if [[ -n "${FOREVERTAS_CUDA_ARCHITECTURES:-}" ]]; then
    cmake_args+=(
        "-DCMAKE_CUDA_ARCHITECTURES=${FOREVERTAS_CUDA_ARCHITECTURES}"
    )
fi
if [[ -n "${FOREVERTAS_VALIDATOR_SOURCE:-}" ]]; then
    cmake_args+=(
        "-DFETCHCONTENT_SOURCE_DIR_FOREVERVALIDATOR=${FOREVERTAS_VALIDATOR_SOURCE}"
    )
fi
cmake "${cmake_args[@]}"
cmake --build "${build_dir}" --parallel

rm -rf "${appdir}"
DESTDIR="${appdir}" cmake --install "${build_dir}"

desktop-file-validate \
    "${appdir}/usr/share/applications/dev.skycrafter.forevertas.desktop"
appstreamcli validate --no-net \
    "${appdir}/usr/share/metainfo/dev.skycrafter.forevertas.appdata.xml"

version="$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "${build_dir}/CMakeCache.txt")"
if [[ -z "${version}" ]]; then
    version="0.0.0"
fi

find_qt6_qmake() {
    local candidate resolved candidate_version
    local candidates=()

    if [[ -n "${QMAKE:-}" ]]; then
        candidates+=("${QMAKE}")
    fi
    candidates+=(qmake6 qmake-qt6 qmake)

    for candidate in "${candidates[@]}"; do
        if [[ "${candidate}" == */* ]]; then
            resolved="${candidate}"
            [[ -x "${resolved}" ]] || continue
        else
            resolved="$(command -v "${candidate}" 2>/dev/null || true)"
            [[ -n "${resolved}" ]] || continue
        fi

        candidate_version="$("${resolved}" -query QT_VERSION 2>/dev/null || true)"
        if [[ "${candidate_version}" == 6.* ]]; then
            printf '%s\n' "${resolved}"
            return 0
        fi
    done

    echo "Could not find a Qt 6 qmake executable. Set QMAKE explicitly." >&2
    return 1
}

real_qmake="$(find_qt6_qmake)"
real_qt_plugins="$("${real_qmake}" -query QT_INSTALL_PLUGINS)"
if [[ ! -d "${real_qt_plugins}" ]]; then
    echo "Qt plugin directory does not exist: ${real_qt_plugins}" >&2
    exit 1
fi

# Distribution Qt installations can contain third-party plugin packs unrelated
# to ForeverTAS. Present a filtered plugin tree to linuxdeploy so optional KDE
# image format plugins cannot pull in unavailable codecs or inflate the bundle.
filtered_qt_plugins="${build_dir}/qt-plugins-forevertas"
rm -rf "${filtered_qt_plugins}"
mkdir -p "${filtered_qt_plugins}"
cp -a "${real_qt_plugins}/." "${filtered_qt_plugins}/"

filter_plugin_directory() {
    local directory="$1"
    shift
    rm -rf "${filtered_qt_plugins}/${directory}"
    mkdir -p "${filtered_qt_plugins}/${directory}"

    local plugin
    for plugin in "$@"; do
        if [[ -e "${real_qt_plugins}/${directory}/${plugin}" ]]; then
            cp -a "${real_qt_plugins}/${directory}/${plugin}" \
                "${filtered_qt_plugins}/${directory}/"
        fi
    done
}

filter_plugin_directory imageformats \
    libqgif.so \
    libqico.so \
    libqjpeg.so \
    libqsvg.so \
    libqwebp.so
filter_plugin_directory platforminputcontexts \
    libcomposeplatforminputcontextplugin.so \
    libibusplatforminputcontextplugin.so
rm -rf "${filtered_qt_plugins}/platformthemes" \
       "${filtered_qt_plugins}/styles"

qmake_wrapper="${build_dir}/qmake-forevertas"
cat > "${qmake_wrapper}" <<'QMAKE_WRAPPER'
#!/usr/bin/env bash
set -euo pipefail

if [[ "${1:-}" == "-query" && "$#" -eq 1 ]]; then
    while IFS= read -r line; do
        if [[ "${line}" == QT_INSTALL_PLUGINS:* ]]; then
            printf 'QT_INSTALL_PLUGINS:%s\n' "${FOREVERTAS_FILTERED_QT_PLUGINS}"
        else
            printf '%s\n' "${line}"
        fi
    done < <("${FOREVERTAS_REAL_QMAKE}" -query)
else
    exec "${FOREVERTAS_REAL_QMAKE}" "$@"
fi
QMAKE_WRAPPER
chmod +x "${qmake_wrapper}"

export FOREVERTAS_REAL_QMAKE="${real_qmake}"
export FOREVERTAS_FILTERED_QT_PLUGINS="${filtered_qt_plugins}"
export QMAKE="${qmake_wrapper}"
export QML_SOURCES_PATHS="${repo_root}/qml"
export EXTRA_PLATFORM_PLUGINS="${EXTRA_PLATFORM_PLUGINS:-libqoffscreen.so}"
output="${dist_dir}/ForeverTAS-${version}-linux-${appimage_arch}.AppImage"
rm -f "${output}" "${output}.sha256"
export LDAI_OUTPUT="${output}"
export APPIMAGE_EXTRACT_AND_RUN=1
export PATH="$(dirname "${qt_plugin}"):${PATH}"

if [[ "${FOREVERTAS_ENABLE_STRIP:-0}" == "1" ]]; then
    unset NO_STRIP
else
    export NO_STRIP=1
fi

"${linuxdeploy}" \
    --appdir "${appdir}" \
    --desktop-file "${appdir}/usr/share/applications/dev.skycrafter.forevertas.desktop" \
    --icon-file "${appdir}/usr/share/icons/hicolor/256x256/apps/dev.skycrafter.forevertas.png" \
    --exclude-library 'libcuda.so*' \
    --executable "${appdir}/usr/bin/ForeverTAS" \
    --plugin qt \
    --output appimage

QT_QPA_PLATFORM=offscreen \
QSG_RHI_BACKEND=software \
APPIMAGE_EXTRACT_AND_RUN=1 \
    "${output}" --qml-smoke-test

(
    cd "${dist_dir}"
    sha256sum "$(basename "${output}")" > "$(basename "${output}").sha256"
)
echo "Created ${output}"
