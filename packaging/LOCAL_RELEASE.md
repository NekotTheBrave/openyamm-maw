# Local release build

`packaging/build_release_local.sh` builds the same packages as the CI workflow (`.github/workflows/nightly.yml`)
on an Ubuntu machine, for when GitHub Actions is unavailable:

```
build_release/flatpak/OpenYAMM-<version>-x86_64.flatpak
build_release/windows/OpenYAMM-<version>-windows-x64.zip
build_release/android/OpenYAMM-<version>-android-arm64.apk
```

Each package has a `.sha256` next to it. Run the script from a fresh clone; it refuses to run if `build/` holds a
development build, because Flatpak asset staging and Gradle share the host tool build in `build/`.

```sh
git clone --depth 1 git@github.com:pjasicek/openyamm.git openyamm-release
cd openyamm-release
packaging/build_release_local.sh                      # nightly: all three packages
packaging/build_release_local.sh --version=0.8        # tagged-release names and Flatpak branch
packaging/build_release_local.sh windows flatpak      # only some packages
```

The `assets_source` submodule is not needed: CI and this script package the committed `assets_dev/` and
`assets_cooked/android/` files.

## Ubuntu packages

Package names for Ubuntu 26.04 (checked against its archive):

```sh
sudo apt install \
    git cmake ninja-build make g++ pkgconf python3 unzip \
    libgl1-mesa-dev libx11-dev libxext-dev \
    flatpak flatpak-builder ostree elfutils librsvg2-common \
    gcc-mingw-w64-x86-64-posix g++-mingw-w64-x86-64-posix binutils-mingw-w64-x86-64 \
    openjdk-17-jdk-headless
```

| Group | Packages |
| --- | --- |
| Host tools (all targets) | `git cmake ninja-build make g++ pkgconf python3` (3.11 or newer), `libgl1-mesa-dev libx11-dev libxext-dev` |
| Flatpak | `flatpak flatpak-builder ostree elfutils librsvg2-common`; the script adds the user Flathub remote and installs `org.freedesktop.Platform`/`Sdk` 24.08 itself |
| Windows | `gcc-mingw-w64-x86-64-posix g++-mingw-w64-x86-64-posix binutils-mingw-w64-x86-64` |
| Android | `openjdk-17-jdk-headless unzip`, plus the Android SDK below |
| Optional | `wine` to start the Windows build locally |

## Android SDK

The SDK does not come from apt. Install Google's command-line tools once, at
`~/android-sdk/cmdline-tools/latest/` (or set `ANDROID_HOME`): download *Command line tools only* for Linux from
<https://developer.android.com/studio#command-line-tools-only>, unzip it, and move its `cmdline-tools/` folder to
`~/android-sdk/cmdline-tools/latest/`. The script then accepts the licences and installs `platforms;android-35`,
`build-tools;35.0.0`, `ndk;28.2.13676358` and `cmake;3.22.1` itself.

## Signing keys

Secrets stay outside the clone, in `~/openyamm-keys/` (or `$OPENYAMM_KEYS_DIR`):

```
~/openyamm-keys/            chmod 700
  android-release.jks       the existing Android release keystore
  release.env               chmod 600
```

`release.env` holds `OPENYAMM_*=value` lines (blank lines and `#` comments allowed, values optionally quoted). The
script reads them without executing the file, and variables already set in the environment win:

```sh
OPENYAMM_ANDROID_KEYSTORE_PASSWORD='...'
# OPENYAMM_ANDROID_KEY_PASSWORD='...'       # default: the keystore password
# OPENYAMM_ANDROID_KEY_ALIAS=openyamm       # default
# OPENYAMM_ANDROID_KEYSTORE=other.jks       # default: android-release.jks; relative paths are in this folder
```

The script never creates a key, since a new key breaks updates of installed APKs. It checks the keystore,
alias and password with `keytool` before building, prompts for the password when `release.env` lacks it, and warns
when the folder or its files are readable by other users. Use the same keystore as the CI secret
`OPENYAMM_ANDROID_KEYSTORE_BASE64`, or local and CI APKs will not update each other.

## Android version codes

`packaging/android_version_code.sh` gives every APK, from CI or from this script, the number of seconds from
2026-01-01 UTC to its commit's committer time. A package of a newer commit therefore updates any package of an
older commit, whichever of the two is a nightly or a tagged release. Packages of the same commit (a re-run, or a tag
on the commit of the last nightly) share a code; Android installs those over each other as a reinstall. The codes
start above every code of the earlier run-number scheme and reach Android's limit in 2092.

This relies on committer times increasing along `main`, which rebases and normal commits preserve. A commit made
with a clock running behind could produce an APK that does not update the one before it.

## Windows cross build

`cmake/toolchains/mingw-w64-x86_64.cmake` uses Ubuntu's MinGW-w64 GCC with the POSIX thread model and links
libgcc, libstdc++ and winpthread statically, so the zip needs no compiler DLLs; the script fails if `openyamm.exe`
imports any. The cross build takes `openyamm_shaderc` and `openyamm_sprite_atlas_cook` from the host build through
`OPENYAMM_HOST_SHADERC` and `OPENYAMM_HOST_SPRITE_ATLAS_COOK`.

Differences from CI's MSYS2 build: Ubuntu's MinGW-w64 is GCC 13 against the older MSVCRT C runtime, while MSYS2
UCRT64 uses a current GCC against UCRT. Both run on Windows 10 and 11. Check a local build on Windows (or under
Wine) before publishing it.
