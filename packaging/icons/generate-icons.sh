#!/usr/bin/env bash
set -euo pipefail

icon_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source_svg="${icon_dir}/dev.skycrafter.forevertas.svg"
python="${PYTHON:-python3}"
mode="${1:-write}"

if [[ "${mode}" != "write" && "${mode}" != "--check" ]]; then
    echo "Usage: $0 [--check]" >&2
    exit 2
fi

for command in inkscape magick "${python}"; do
    if ! command -v "${command}" >/dev/null 2>&1; then
        echo "Required icon-generation command not found: ${command}" >&2
        exit 1
    fi
done
if ! "${python}" -c "import PIL" 2>/dev/null; then
    echo "Python package Pillow is required to generate the macOS icon." >&2
    exit 1
fi

work_dir="$(mktemp -d)"
trap 'rm -rf "${work_dir}"' EXIT

for size in 16 24 32 48 64 128 256 512 1024; do
    inkscape "${source_svg}" \
        --export-area-page \
        --export-width="${size}" \
        --export-height="${size}" \
        --export-filename="${work_dir}/${size}.png" \
        >/dev/null
done

magick \
    "${work_dir}/256.png" \
    "${work_dir}/128.png" \
    "${work_dir}/64.png" \
    "${work_dir}/48.png" \
    "${work_dir}/32.png" \
    "${work_dir}/24.png" \
    "${work_dir}/16.png" \
    "${work_dir}/ForeverTAS.ico"

FOREVERTAS_ICON_WORK_DIR="${work_dir}" "${python}" <<'PY'
import os
from pathlib import Path

from PIL import Image

work_dir = Path(os.environ["FOREVERTAS_ICON_WORK_DIR"])
with Image.open(work_dir / "1024.png") as icon:
    icon.save(work_dir / "ForeverTAS.icns", format="ICNS")
PY

declare -A generated_assets=(
    ["dev.skycrafter.forevertas.png"]="${work_dir}/256.png"
    ["ForeverTAS.ico"]="${work_dir}/ForeverTAS.ico"
    ["ForeverTAS.icns"]="${work_dir}/ForeverTAS.icns"
)

if [[ "${mode}" == "--check" ]]; then
    status=0
    for asset in "${!generated_assets[@]}"; do
        if ! cmp -s "${generated_assets[${asset}]}" "${icon_dir}/${asset}"; then
            echo "Generated icon is stale: packaging/icons/${asset}" >&2
            status=1
        fi
    done
    exit "${status}"
fi

for asset in "${!generated_assets[@]}"; do
    cp "${generated_assets[${asset}]}" "${icon_dir}/${asset}"
done

echo "Updated platform icons from packaging/icons/$(basename "${source_svg}")"
