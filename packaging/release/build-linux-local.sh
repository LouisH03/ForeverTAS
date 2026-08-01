#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
manifest="${1:-${repo_root}/packaging/release/manifest.json}"
cold="${2:-}"

eval "$(python3 - "${manifest}" <<'PY'
import json, shlex, sys
m = json.load(open(sys.argv[1], encoding="utf-8"))
values = {
    "FOREVERTAS_VERSION": m["release"]["version"],
    "CUDA_VERSION": m["cuda"]["version"],
    "CUDA_ARCHITECTURES": m["cuda"]["cmake_architectures"],
    "CUDA_ARCHITECTURE_KEY": m["cuda"]["architecture_key"],
    "FOREVERVALIDATOR_CUDA_SPLIT_COMPILE_JOBS": str(m["cuda"]["split_compile_jobs"]),
    "FOREVERVALIDATOR_COMMIT": m["sources"]["forevervalidator"]["commit"],
}
for key, value in values.items():
    print(f"export {key}={shlex.quote(value)}")
PY
)"

cache_root="${FOREVERTAS_RELEASE_CACHE:-${repo_root}/.release-cache/linux}"
toolchain_image="$(${repo_root}/packaging/release/ensure-linux-toolchain.sh)"
if [[ "${cold}" == "--cold" ]]; then
    rm -rf "${cache_root}"
fi
mkdir -p "${cache_root}/sccache" "${cache_root}/cuda-search"

docker run --rm --init \
    --user "$(id -u):$(id -g)" \
    --tmpfs "/home/builder:rw,uid=$(id -u),gid=$(id -g),mode=0755" \
    --env HOME=/home/builder \
    --env CUDA_VERSION \
    --env CUDA_ARCHITECTURES \
    --env CUDA_ARCHITECTURE_KEY \
    --env FOREVERVALIDATOR_COMMIT \
    --env FOREVERVALIDATOR_CUDA_SPLIT_COMPILE_JOBS \
    --env FOREVERTAS_VERSION \
    --env FOREVERTAS_TOOLCHAIN_IMAGE="${toolchain_image}" \
    --env SCCACHE_CACHE_SIZE=50G \
    --volume "${repo_root}:/workspace" \
    --volume "${cache_root}:/cache" \
    --workdir /workspace \
    "${toolchain_image}" \
    bash packaging/release/package-linux.sh

test -s "${repo_root}/dist/cuda-fatbinary-linux.json"
printf 'PASS Linux local release build (%s cache)\n' "$([[ "${cold}" == "--cold" ]] && echo cold || echo warm)"
