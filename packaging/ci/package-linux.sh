#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
validator_root="${repo_root}/.dependencies/ForeverValidator"
build_dir="${repo_root}/build/ci"
dist_dir="${repo_root}/dist"
cache_root="${FOREVERTAS_CACHE_ROOT:-/cache}"
search_cache_root="${cache_root}/cuda-search"
split_compile_jobs="${FOREVERVALIDATOR_CUDA_SPLIT_COMPILE_JOBS:-4}"

: "${CUDA_VERSION:?CUDA_VERSION is required}"
: "${CUDA_ARCHITECTURES:?CUDA_ARCHITECTURES is required}"
: "${CUDA_ARCHITECTURE_KEY:?CUDA_ARCHITECTURE_KEY is required}"
: "${FOREVERVALIDATOR_COMMIT:?FOREVERVALIDATOR_COMMIT is required}"
: "${FOREVERTAS_TOOLCHAIN_IMAGE:?FOREVERTAS_TOOLCHAIN_IMAGE is required}"

actual_validator_commit="$(git -C "${validator_root}" rev-parse HEAD)"
if [[ "${actual_validator_commit}" != "${FOREVERVALIDATOR_COMMIT}" ]]; then
    echo "ForeverValidator checkout does not match the pinned commit" >&2
    exit 1
fi

export SCCACHE_DIR="${cache_root}/sccache"
export SCCACHE_CACHE_SIZE="${SCCACHE_CACHE_SIZE:-50G}"
unset SCCACHE_GHA_ENABLED SCCACHE_ENDPOINT SCCACHE_GHA_VERSION
mkdir -p "${SCCACHE_DIR}" "${search_cache_root}"

sccache --start-server
sccache --zero-stats
report_cache() {
    sccache --show-stats || true
    sccache --stop-server || true
}
trap report_cache EXIT

search_key="$({
    printf '%s\n' \
        "toolchain=${FOREVERTAS_TOOLCHAIN_IMAGE}" \
        "cuda=${CUDA_VERSION}" \
        "architectures=${CUDA_ARCHITECTURES}" \
        "architecture_key=${CUDA_ARCHITECTURE_KEY}" \
        "split_compile_jobs=${split_compile_jobs}" \
        "validator=${actual_validator_commit}"
    nvcc --version
    c++ -dumpfullversion -dumpversion
    sha256sum "${repo_root}/CMakeLists.txt" "${validator_root}/CMakeLists.txt"
} | sha256sum | cut -d' ' -f1)"
search_cache_dir="${search_cache_root}/${search_key}"
cached_search_object="${search_cache_dir}/cuda_search_executor.cu.o"

verify_architectures() {
    local object="$1"
    local output architecture
    [[ -f "${object}" ]] || return 1
    output="$("${CUDA_PATH}/bin/cuobjdump" --list-elf "${object}")" || return 1
    for architecture in 50 52 61 70 75 86 89 120; do
        grep -q "sm_${architecture}\.cubin" <<<"${output}" || return 1
    done
    output="$("${CUDA_PATH}/bin/cuobjdump" --list-ptx "${object}")" || return 1
    grep -q "sm_120\.ptx" <<<"${output}"
}

prebuilt_option="-DFOREVERVALIDATOR_CUDA_SEARCH_PREBUILT_OBJECT="
cache_hit=false
if verify_architectures "${cached_search_object}"; then
    cache_hit=true
    prebuilt_option="-DFOREVERVALIDATOR_CUDA_SEARCH_PREBUILT_OBJECT=${cached_search_object}"
    echo "Reusing cached CUDA search object ${search_key}"
elif [[ -e "${search_cache_dir}" ]]; then
    echo "Discarding invalid CUDA search object cache ${search_key}" >&2
    rm -rf "${search_cache_dir}"
fi

rm -rf "${build_dir}" "${dist_dir}"
mkdir -p "${build_dir}" "${dist_dir}"

cmake -S "${repo_root}" -B "${build_dir}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    "-DCMAKE_CUDA_ARCHITECTURES=${CUDA_ARCHITECTURES}" \
    "-DCMAKE_CUDA_COMPILER_LAUNCHER=python3;${repo_root}/packaging/ci/cuda_compiler_launcher.py;sccache" \
    -DCMAKE_CXX_COMPILER_LAUNCHER=sccache \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DBUILD_TESTING=OFF \
    -DFOREVERTAS_ENABLE_CUDA=ON \
    "-DFOREVERVALIDATOR_CUDA_SPLIT_COMPILE_JOBS=${split_compile_jobs}" \
    "${prebuilt_option}" \
    "-DFETCHCONTENT_SOURCE_DIR_FOREVERVALIDATOR=${validator_root}"

grep -q 'FOREVERTAS_ENABLE_CUDA:BOOL=ON' "${build_dir}/CMakeCache.txt"
grep -q 'FOREVERVALIDATOR_HAS_CUDA=1' "${build_dir}/compile_commands.json"
grep -q "FOREVERVALIDATOR_CUDA_SPLIT_COMPILE_JOBS:STRING=${split_compile_jobs}" \
    "${build_dir}/CMakeCache.txt"
if [[ "${cache_hit}" == true ]]; then
    grep -Fq "FOREVERVALIDATOR_CUDA_SEARCH_PREBUILT_OBJECT:FILEPATH=${cached_search_object}" \
        "${build_dir}/CMakeCache.txt"
    if grep -q 'cuda_search_executor\.cu' "${build_dir}/compile_commands.json"; then
        echo "Cached CUDA search object was not used" >&2
        exit 1
    fi
else
    grep -q 'cuda_search_executor\.cu' "${build_dir}/compile_commands.json"
fi

cmake --build "${build_dir}" --parallel

built_search_object="${build_dir}/_deps/forevervalidator-build/CMakeFiles/forevervalidator_core.dir/src/simulation/backends/cuda/cuda_search_executor.cu.o"
if [[ "${cache_hit}" == false ]]; then
    verify_architectures "${built_search_object}"
    temporary_cache_dir="${search_cache_dir}.tmp.$$"
    rm -rf "${temporary_cache_dir}"
    mkdir -p "${temporary_cache_dir}"
    cp "${built_search_object}" \
        "${temporary_cache_dir}/cuda_search_executor.cu.o"
    printf '%s\n' \
        "toolchain=${FOREVERTAS_TOOLCHAIN_IMAGE}" \
        "cuda=${CUDA_VERSION}" \
        "architectures=${CUDA_ARCHITECTURES}" \
        "split_compile_jobs=${split_compile_jobs}" \
        "validator=${actual_validator_commit}" \
        > "${temporary_cache_dir}/metadata.txt"
    mv "${temporary_cache_dir}" "${search_cache_dir}"
    cached_search_object="${search_cache_dir}/cuda_search_executor.cu.o"
    echo "Cached CUDA search object ${search_key}"
fi

verify_architectures "${cached_search_object}"
"${CUDA_PATH}/bin/cuobjdump" --list-elf "${cached_search_object}" \
    | tee "${build_dir}/cuda-elf-images.txt"
"${CUDA_PATH}/bin/cuobjdump" --list-ptx "${cached_search_object}" \
    | tee "${build_dir}/cuda-ptx-images.txt"

FOREVERTAS_BUILD_DIR="${build_dir}" \
FOREVERTAS_DIST_DIR="${dist_dir}" \
FOREVERTAS_ENABLE_CUDA=ON \
FOREVERTAS_CUDA_ARCHITECTURES="${CUDA_ARCHITECTURES}" \
FOREVERTAS_VALIDATOR_SOURCE="${validator_root}" \
    "${repo_root}/packaging/linux/build-appimage.sh"
