#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build_dir="${FOREVERTAS_BUILD_DIR:-${repo_root}/build/package-macos}"
dist_dir="${FOREVERTAS_DIST_DIR:-${repo_root}/dist}"

mkdir -p "${dist_dir}"
rm -f "${dist_dir}"/ForeverTAS-*-macos-*.dmg*

cmake -S "${repo_root}" -B "${build_dir}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF
cmake --build "${build_dir}" --parallel
cpack --config "${build_dir}/CPackConfig.cmake" \
    -G DragNDrop -B "${dist_dir}"

shopt -s nullglob
artifacts=("${dist_dir}"/ForeverTAS-*-macos-*.dmg)
if (( ${#artifacts[@]} != 1 )); then
    echo "Expected one macOS DMG, found ${#artifacts[@]}" >&2
    exit 1
fi

artifact="${artifacts[0]}"
shasum -a 256 "${artifact}" > "${artifact}.sha256"
echo "Created ${artifact}"
