#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
output="${root}/assets/materials"
mkdir -p "${output}"

generate_pair() {
    local name="$1"
    local color="$2"
    local seed="$3"
    local strength="$4"
    local base_marks=()
    local normal_marks=()

    # Avoid ImageMagick's platform-dependent noise generator. These positions
    # are a tiny integer hash, so regenerating the assets is byte-for-byte
    # stable while every material still receives a distinct fine pattern.
    for ((i = 0; i < 48; ++i)); do
        local x=$(((seed * 17 + i * 29 + i * i * 3) % 128))
        local y=$(((seed * 31 + i * 43 + i * i * 5) % 128))
        local radius=$((1 + (seed + i) % 3))
        local nx=$(((x + 3 + i % 5) % 128))
        local ny=$(((y + 5 + i % 7) % 128))
        base_marks+=(
            -fill "rgba(255,255,255,${strength})"
            -draw "circle ${x},${y} $((x + radius)),${y}"
        )
        normal_marks+=(
            -fill 'rgba(116,140,246,0.28)'
            -draw "circle ${nx},${ny} $((nx + radius)),${ny}"
        )
    done

    magick -size 128x128 "xc:${color}" "${base_marks[@]}" \
        -blur 0x0.35 -colorspace sRGB -depth 8 \
        -define png:exclude-chunk=date,time \
        "${output}/${name}_base.png"
    magick -size 128x128 xc:'#8080ff' "${normal_marks[@]}" \
        -blur 0x0.45 -colorspace sRGB -depth 8 \
        -define png:exclude-chunk=date,time \
        "${output}/${name}_normal.png"
}

generate_pair asphalt '#5d615f' 101 0.11
generate_pair concrete '#a4a69f' 102 0.08
generate_pair dirt '#816548' 103 0.13
generate_pair grass '#4f7143' 104 0.12
generate_pair metal '#8e9597' 105 0.035
generate_pair painted_metal '#a94a3c' 106 0.045
generate_pair plastic '#d5d7d0' 107 0.025
generate_pair rubber '#343735' 108 0.06
generate_pair glass '#9bd2dc' 109 0.012
generate_pair signage '#ede8d6' 110 0.025
generate_pair emissive '#d9f5e7' 111 0.018
generate_pair water '#4a98aa' 112 0.055
generate_pair neutral '#969b97' 113 0.045
generate_pair unknown '#9f849d' 114 0.06

# Add deterministic, clearly synthetic markings to the two graphic classes.
magick "${output}/signage_base.png" \
    -stroke '#cf3434' -strokewidth 12 -draw 'line 0,32 128,32' \
    -stroke '#315c91' -strokewidth 8 -draw 'line 0,92 128,92' \
    -define png:exclude-chunk=date,time \
    "${output}/signage_base.png"
magick "${output}/emissive_base.png" \
    -fill '#55ffc0' -draw 'rectangle 0,52 128,76' \
    -define png:exclude-chunk=date,time \
    "${output}/emissive_base.png"
