#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(CDPATH= cd -- "${script_dir}/.." && pwd)

assets_dev_dir="${repo_root}/assets_dev"
assets_dir="${OPENYAMM_ANDROID_ASSETS_DIR:-${repo_root}/build/android-assets}"
host_build_dir="${OPENYAMM_HOST_BUILD_DIR:-${repo_root}/build}"
sprite_dir="${repo_root}/assets_cooked/android/sprites_new"
cooker="${host_build_dir}/game/openyamm_sprite_atlas_cook"

cmake --build "${host_build_dir}" --target openyamm_sprite_atlas_cook \
    --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-25}"
python3 "${repo_root}/tools/package_runtime_assets.py" \
    --assets-root "${assets_dev_dir}" --output "${assets_dir}" \
    --profile android --sprites "${sprite_dir}" --cooker "${cooker}" \
    --world mm6 --world mm7 --world mm8
