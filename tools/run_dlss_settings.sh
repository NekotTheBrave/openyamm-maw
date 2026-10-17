#!/usr/bin/env bash
# Live controls for the isolated Vulkan/DLSS experiment.
set -euo pipefail

repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
test_dir="$repo_dir/build-vulkan/dlss-test"
gui_binary="$test_dir/gui/dlssnr_gui"

if [[ ! -x "$gui_binary" ]]; then
    echo "DLSS settings GUI is not built; see tools/VULKAN_DLSS_TEST.md." >&2
    exit 1
fi

export XDG_CONFIG_HOME="$test_dir/gui-config"
export XDG_DATA_HOME="$test_dir/ge-data"
export XDG_STATE_HOME="$test_dir/state"
export DLSSNR_SHM="$test_dir/shm.bin"
export DLSSNR_LOG="$test_dir/ge-helper.log"
# This local SDK was built without fontconfig; use the installed desktop fonts.
export QT_QPA_FONTDIR="${QT_QPA_FONTDIR:-/usr/share/fonts/truetype/dejavu}"

mkdir -p "$XDG_CONFIG_HOME/dlssnr"
config_file="$XDG_CONFIG_HOME/dlssnr/config.ini"
if [[ ! -e "$config_file" ]]; then
    printf 'binaries=%s\nshm=%s\nlog=%s\n' \
        "$test_dir/ge-helper" "$DLSSNR_SHM" "$DLSSNR_LOG" > "$config_file"
fi

cd "$test_dir"
exec "$gui_binary" "$@"
