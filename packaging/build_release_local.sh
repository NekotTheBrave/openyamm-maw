#!/usr/bin/env bash
# Builds the CI release packages (.github/workflows/nightly.yml) on a local Ubuntu machine:
#   build_release/flatpak/OpenYAMM-<version>-x86_64.flatpak
#   build_release/windows/OpenYAMM-<version>-windows-x64.zip   (MinGW-w64 cross build)
#   build_release/android/OpenYAMM-<version>-android-arm64.apk
# Each package gets a .sha256 next to it. Run it from a fresh clone; see packaging/LOCAL_RELEASE.md for the
# required Ubuntu packages, Android SDK components and signing key.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"

host_build_dir="$repo_root/build"
windows_build_dir="$repo_root/build-windows-cross"
flatpak_work_dir="$repo_root/build-flatpak-release"
output_root="$repo_root/build_release"

android_ndk_version=28.2.13676358 # Must match ndkVersion in android/app/build.gradle.
android_sdk_packages=("platforms;android-35" "build-tools;35.0.0" "ndk;$android_ndk_version" "cmake;3.22.1")

keys_dir="${OPENYAMM_KEYS_DIR:-$HOME/openyamm-keys}"

version=nightly
jobs="$(( $(nproc) > 2 ? $(nproc) - 2 : 1 ))"
targets=()

usage()
{
    cat <<EOF
Usage: $0 [options] [flatpak] [windows] [android]

Builds the selected release packages (default: all three) into build_release/.

Options:
  --version=X.Y   Release version (CI tag format). Default: nightly
  --jobs=N        Parallel build jobs. Default: online CPUs minus 2 ($jobs)
  -h, --help      Show this help.

Secrets live outside the clone in \$OPENYAMM_KEYS_DIR (default: ~/openyamm-keys):
  android-release.jks   The existing Android release keystore (a new key would break APK updates).
  release.env           OPENYAMM_*=value lines, exported unless already set in the environment:
                          OPENYAMM_ANDROID_KEYSTORE_PASSWORD (prompted when missing)
                          OPENYAMM_ANDROID_KEY_PASSWORD      (default: the keystore password)
                          OPENYAMM_ANDROID_KEY_ALIAS         (default: openyamm)
                          OPENYAMM_ANDROID_KEYSTORE          (default: android-release.jks; relative to the folder)
ANDROID_HOME selects the Android SDK (default: ~/android-sdk).
EOF
}

for argument in "$@"; do
    case "$argument" in
        --version=*) version="${argument#*=}" ;;
        --jobs=*) jobs="${argument#*=}" ;;
        flatpak|windows|android) targets+=("$argument") ;;
        -h|--help) usage; exit 0 ;;
        *) usage >&2; printf 'Unknown argument: %s\n' "$argument" >&2; exit 1 ;;
    esac
done

if [ "${#targets[@]}" -eq 0 ]; then
    targets=(flatpak windows android)
fi

wants()
{
    local target
    for target in "${targets[@]}"; do
        if [ "$target" = "$1" ]; then
            return 0
        fi
    done
    return 1
}

fail()
{
    printf 'error: %s\n' "$*" >&2
    exit 1
}

step()
{
    printf '\n==> %s\n' "$*"
}

warn_if_shared()
{
    local mode
    mode="$(stat -c %a "$1")"
    if (( 8#$mode & 8#077 )); then
        printf 'warning: %s is accessible to other users (mode %s); chmod go-rwx it.\n' "$1" "$mode" >&2
    fi
}

# Reads OPENYAMM_*=value lines from $keys_dir/release.env without executing it; the environment wins.
load_keys()
{
    local env_file="$keys_dir/release.env"
    local line name value line_number=0

    if [ ! -d "$keys_dir" ]; then
        return 0
    fi
    warn_if_shared "$keys_dir"
    if [ ! -f "$env_file" ]; then
        return 0
    fi
    warn_if_shared "$env_file"

    while IFS= read -r line || [ -n "$line" ]; do
        line_number=$(( line_number + 1 ))
        line="${line#"${line%%[![:space:]]*}"}"
        case "$line" in
            ''|'#'*) continue ;;
        esac
        # Report the line number only: a malformed line may hold a password.
        if [[ ! "$line" =~ ^(OPENYAMM_[A-Z0-9_]+)=(.*)$ ]]; then
            fail "$env_file:$line_number: expected OPENYAMM_NAME=value"
        fi
        name="${BASH_REMATCH[1]}"
        value="${BASH_REMATCH[2]}"
        if [[ "$value" =~ ^\"(.*)\"$ ]] || [[ "$value" =~ ^\'(.*)\'$ ]]; then
            value="${BASH_REMATCH[1]}"
        fi
        if [ -z "${!name+x}" ]; then
            export "$name=$value"
        fi
    done < "$env_file"
}

# Release naming, matching the workflow's package_metadata job.
flatpak_branch=nightly
short_sha="$(git -C "$repo_root" rev-parse --short=12 HEAD)"
android_version_name="nightly-$short_sha"
if [ "$version" != nightly ]; then
    if [[ ! "$version" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]] || (( BASH_REMATCH[2] > 99 )) \
        || [ "$version" = 0.0 ]; then
        fail "--version must use canonical X.Y versioning (minor 0-99, above 0.0), for example 0.7."
    fi
    flatpak_branch="$version"
    android_version_name="$version"
fi

flatpak_asset="OpenYAMM-$version-x86_64.flatpak"
windows_asset="OpenYAMM-$version-windows-x64.zip"
android_asset="OpenYAMM-$version-android-arm64.apk"

# Prerequisites: report every missing tool at once, with the Ubuntu package that provides it.
missing=()
require_command()
{
    if ! command -v "$1" >/dev/null 2>&1; then
        missing+=("$1 (apt: $2)")
    fi
}

require_command git git
require_command cmake cmake
require_command ninja ninja-build
require_command make make
require_command g++ g++
require_command python3 python3
require_command pkg-config pkgconf
for header in GL/gl.h X11/Xlib.h X11/extensions/Xext.h; do
    if [ ! -f "/usr/include/$header" ]; then
        missing+=("/usr/include/$header (apt: libgl1-mesa-dev libx11-dev libxext-dev)")
    fi
done

if wants flatpak; then
    require_command flatpak flatpak
    require_command flatpak-builder flatpak-builder
    require_command ostree ostree
    require_command eu-strip elfutils
fi

if wants windows; then
    require_command x86_64-w64-mingw32-gcc-posix gcc-mingw-w64-x86-64-posix
    require_command x86_64-w64-mingw32-g++-posix g++-mingw-w64-x86-64-posix
    require_command x86_64-w64-mingw32-windres binutils-mingw-w64-x86-64
    require_command x86_64-w64-mingw32-objdump binutils-mingw-w64-x86-64
fi

if wants android; then
    export JAVA_HOME="${JAVA_HOME:-/usr/lib/jvm/java-17-openjdk-amd64}"
    if [ ! -x "$JAVA_HOME/bin/keytool" ]; then
        missing+=("JDK 17 at $JAVA_HOME (apt: openjdk-17-jdk-headless)")
    fi
    require_command unzip unzip
fi

if [ "${#missing[@]}" -gt 0 ]; then
    printf 'Missing prerequisites:\n' >&2
    printf '  %s\n' "${missing[@]}" >&2
    exit 1
fi

if wants android; then
    sdkmanager=""
    for sdk_dir in "${ANDROID_HOME:-}" "${ANDROID_SDK_ROOT:-}" "$HOME/android-sdk"; do
        if [ -n "$sdk_dir" ] && [ -x "$sdk_dir/cmdline-tools/latest/bin/sdkmanager" ]; then
            sdkmanager="$sdk_dir/cmdline-tools/latest/bin/sdkmanager"
            break
        fi
    done
    if [ -z "$sdkmanager" ]; then
        fail "Android SDK command-line tools not found in ANDROID_HOME, ANDROID_SDK_ROOT or ~/android-sdk" \
            "(see packaging/LOCAL_RELEASE.md)."
    fi
    export ANDROID_HOME="$sdk_dir"
    export ANDROID_SDK_ROOT="$sdk_dir"

    load_keys
    export OPENYAMM_ANDROID_KEY_ALIAS="${OPENYAMM_ANDROID_KEY_ALIAS:-openyamm}"
    export OPENYAMM_ANDROID_KEYSTORE="${OPENYAMM_ANDROID_KEYSTORE:-android-release.jks}"
    if [[ "$OPENYAMM_ANDROID_KEYSTORE" != /* ]]; then
        OPENYAMM_ANDROID_KEYSTORE="$keys_dir/$OPENYAMM_ANDROID_KEYSTORE"
    fi
    if [ ! -f "$OPENYAMM_ANDROID_KEYSTORE" ]; then
        fail "Android release keystore not found: $OPENYAMM_ANDROID_KEYSTORE (a new key would break APK updates)."
    fi
    warn_if_shared "$OPENYAMM_ANDROID_KEYSTORE"
    if [ -z "${OPENYAMM_ANDROID_KEYSTORE_PASSWORD:-}" ]; then
        if [ ! -t 0 ]; then
            fail "OPENYAMM_ANDROID_KEYSTORE_PASSWORD is required when stdin is not interactive."
        fi
        read -r -s -p "Android keystore password: " OPENYAMM_ANDROID_KEYSTORE_PASSWORD
        echo
        export OPENYAMM_ANDROID_KEYSTORE_PASSWORD
    fi
    export OPENYAMM_ANDROID_KEY_PASSWORD="${OPENYAMM_ANDROID_KEY_PASSWORD:-$OPENYAMM_ANDROID_KEYSTORE_PASSWORD}"
    "$JAVA_HOME/bin/keytool" -list -keystore "$OPENYAMM_ANDROID_KEYSTORE" \
        -storepass "$OPENYAMM_ANDROID_KEYSTORE_PASSWORD" -alias "$OPENYAMM_ANDROID_KEY_ALIAS" >/dev/null \
        || fail "Cannot open key '$OPENYAMM_ANDROID_KEY_ALIAS' in $OPENYAMM_ANDROID_KEYSTORE."

    # Shared with CI: ordered by commit time, so newer commits update older packages, nightly or tagged.
    android_version_code="$("$script_dir/android_version_code.sh")"
    export OPENYAMM_ANDROID_VERSION_NAME="$android_version_name"
    export OPENYAMM_ANDROID_VERSION_CODE="$android_version_code"
fi

# The host build directory is shared with Flatpak asset staging and Gradle (build/openyamm_shaderc,
# build/_deps/sdl3-src). Refuse a developer build there instead of reconfiguring it.
if [ -f "$host_build_dir/CMakeCache.txt" ] \
    && ! grep -Fxq 'OPENYAMM_BUILD_DESKTOP_EXECUTABLE:BOOL=OFF' "$host_build_dir/CMakeCache.txt"; then
    fail "$host_build_dir holds a development build; run this script from a fresh clone."
fi

export CMAKE_BUILD_PARALLEL_LEVEL="$jobs"
printf 'OpenYAMM %s (%s): %s, jobs=%s\n' "$version" "$short_sha" "${targets[*]}" "$jobs"

step "Host tools (shader compiler and sprite atlas cooker)"
cmake -S "$repo_root" -B "$host_build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DOPENYAMM_BUILD_DESKTOP_EXECUTABLE=OFF \
    -DOPENYAMM_BUILD_TESTS=OFF \
    -DOPENYAMM_BUILD_EDITOR=OFF \
    -DOPENYAMM_BUILD_TOOLS=OFF \
    -DSDL_ALSA=OFF -DSDL_JACK=OFF -DSDL_KMSDRM=OFF -DSDL_PIPEWIRE=OFF -DSDL_PULSEAUDIO=OFF \
    -DSDL_SNDIO=OFF -DSDL_UNIX_CONSOLE_BUILD=ON -DSDL_WAYLAND=OFF -DSDL_X11=OFF
cmake --build "$host_build_dir" --target openyamm_shaderc openyamm_sprite_atlas_cook --parallel "$jobs"

write_checksum()
{
    (cd "$(dirname "$1")" && sha256sum "$(basename "$1")" > "$(basename "$1").sha256")
}

if wants flatpak; then
    step "Flatpak x86_64"
    mkdir -p "$output_root/flatpak"
    rm -f "$output_root/flatpak/$flatpak_asset" "$output_root/flatpak/$flatpak_asset.sha256"
    "$repo_root/packaging/flatpak/build_flatpak.sh" \
        --output-dir="$flatpak_work_dir" \
        --bundle="$output_root/flatpak/$flatpak_asset" \
        --branch="$flatpak_branch" \
        --no-install \
        --add-flathub \
        --install-deps \
        --jobs="$jobs"
    test -s "$output_root/flatpak/$flatpak_asset"
    write_checksum "$output_root/flatpak/$flatpak_asset"
fi

if wants windows; then
    step "Windows x64 (MinGW-w64 cross build)"
    cmake -S "$repo_root" -B "$windows_build_dir" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$repo_root/cmake/toolchains/mingw-w64-x86_64.cmake" \
        -DCMAKE_BUILD_TYPE=Release \
        -DOPENYAMM_BUILD_DESKTOP_EXECUTABLE=ON \
        -DOPENYAMM_BUILD_TESTS=OFF \
        -DOPENYAMM_BUILD_EDITOR=OFF \
        -DOPENYAMM_BUILD_TOOLS=OFF \
        -DOPENYAMM_HOST_SHADERC="$host_build_dir/openyamm_shaderc" \
        -DOPENYAMM_HOST_SPRITE_ATLAS_COOK="$host_build_dir/game/openyamm_sprite_atlas_cook"
    cmake --build "$windows_build_dir" --target openyamm_windows_release_zip --parallel "$jobs"

    archive="$windows_build_dir/release/OpenYAMM-windows-Release.zip"
    contents="$windows_build_dir/release/windows-package-contents.txt"
    test -s "$archive"
    unzip -Z1 "$archive" > "$contents"
    for entry in openyamm.exe settings.ini \
        runtime/shaders/glsl/vs_outdoor_billboard_lit.bin runtime/shaders/glsl/fs_sprite_atlas.bin \
        runtime/shaders/glsl/vs_model.bin runtime/shaders/glsl/vs_model_shadow.bin \
        assets/engine.zip assets/worlds/mm6.zip assets/worlds/mm7.zip assets/worlds/mm8.zip; do
        grep -Fxq "OpenYAMM/$entry" "$contents" || fail "Windows package lacks OpenYAMM/$entry"
    done
    if grep -Eq '^OpenYAMM/runtime/shaders/dxbc(/|$)' "$contents"; then
        fail "Windows package contains unused DirectX shaders."
    fi
    # The toolchain links the MinGW runtime statically; the executable may import only Windows system DLLs.
    imports="$(x86_64-w64-mingw32-objdump -p "$windows_build_dir/game/openyamm.exe" | sed -n 's/^\s*DLL Name: //p')"
    if grep -Eiq '^(libstdc\+\+|libgcc|libwinpthread|SDL3)' <<< "$imports"; then
        fail "openyamm.exe imports non-system DLLs: $(tr '\n' ' ' <<< "$imports")"
    fi

    mkdir -p "$output_root/windows"
    cp "$archive" "$output_root/windows/$windows_asset"
    write_checksum "$output_root/windows/$windows_asset"
fi

if wants android; then
    step "Android arm64"
    yes | "$sdkmanager" --sdk_root="$ANDROID_HOME" --licenses >/dev/null || true
    "$sdkmanager" --sdk_root="$ANDROID_HOME" --install "${android_sdk_packages[@]}" >/dev/null
    test -x "$ANDROID_HOME/ndk/$android_ndk_version/ndk-build"

    "$repo_root/android/build_release_apk.sh" --repack-assets

    apk="$output_root/android/$android_asset"
    mkdir -p "$output_root/android"
    test -s "$repo_root/android/app-release.apk"
    cp "$repo_root/android/app-release.apk" "$apk"
    build_tools="$ANDROID_HOME/build-tools/35.0.0"
    "$build_tools/apksigner" verify "$apk"
    "$build_tools/aapt" dump badging "$apk" \
        | grep -Fq "versionCode='$android_version_code' versionName='$android_version_name'" \
        || fail "APK version does not match $android_version_code / $android_version_name."
    apk_contents="$(unzip -Z1 "$apk")"
    grep -Fxq "lib/arm64-v8a/libmain.so" <<< "$apk_contents" || fail "APK lacks lib/arm64-v8a/libmain.so"
    if grep -Eq '^lib/(armeabi-v7a|x86|x86_64)/' <<< "$apk_contents"; then
        fail "APK contains an unexpected non-arm64 ABI."
    fi
    write_checksum "$apk"
fi

step "Done"
find "$output_root" -type f \( -name '*.flatpak' -o -name '*.zip' -o -name '*.apk' \) -printf '%p  %s bytes\n' \
    | sort
