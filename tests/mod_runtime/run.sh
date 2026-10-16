#!/usr/bin/env bash
# Runs the mod runtime's checks headlessly and prints one line per check.
#
#   tests/mod_runtime/run.sh            everything
#   tests/mod_runtime/run.sh quick      skip the slower regression scenarios
#
# Run from Git Bash. Checks marked (MAW) use the repository's mods.yml and are skipped when it is absent.

set -u

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$root" || exit 2

# The build tools' C++ runtime must come before Git's older copy.
export PATH="/c/Strawberry/c/bin:$PATH"

game="./build/game/openyamm.exe"
tests="tests/mod_runtime"
mode="${1:-all}"
log="$(mktemp)"
failed=0

# The runtime's own log and report go here, not beside mods.yml, where they are the player's last session's.
export OPENYAMM_MOD_LOG_DIR="$root/$tests/logs"

if [ ! -x "$game" ]; then
    echo "missing $game: build the game first (see MAW_PORT_PLAN.md, M0)"
    exit 2
fi

report() {
    local label="$1" status="$2" detail="$3"
    printf '%-58s %s %s\n' "$label" "$status" "$detail"
    [ "$status" = "ok" ] || failed=$((failed + 1))
}

scenario() {
    local label="$1" path="$2"
    timeout 300 "$game" --headless-run-scenario "$path" >"$log" 2>&1
    local code=$?
    local passed
    passed="$(grep -ac 'PASS step' "$log")"

    if [ "$code" -eq 0 ]; then
        report "$label" ok "($passed steps)"
    else
        local detail
        detail="$(grep -a 'Scenario failure' "$log" | sed 's/\x1b\[[0-9;]*m//g; s/^Scenario failure: //' \
            | sed 's/ mod_lua expected / expected /' | cut -c1-260)"

        # No failed check to quote: the game stopped by itself (a crash, or the time limit). Keep its output.
        if [ -z "$detail" ]; then
            local kept="$root/logs/failed_$(basename "$path" .yml).log"
            mkdir -p "$root/logs"
            cp "$log" "$kept"
            detail="exit code $code after $passed steps, no failed check; output kept in ${kept#$root/}"
        fi

        report "$label" FAILED "$detail"
    fi
}

selftest() {
    local label="$1"
    local output
    output="$("$game" --headless-mod-report 0 "return ModRT.selfTest()" 2>/dev/null | grep -a '^selftest')"

    case "$output" in
        *", 0 failed"*) report "$label" ok "($output)" ;;
        *) report "$label" FAILED "$output" ;;
    esac
}

# The menu layouts (assets_dev/engine/ui) parse as YAML: a menu screen ends the game on one that does not (a plain
# value with ": " in it did, on 2026-10-15). Needs Python with PyYAML; skipped without.
ui_layouts() {
    local python
    python="$(command -v python || command -v python3)"

    if [ -z "$python" ] || ! "$python" -c "import yaml" 2>/dev/null; then
        report "menu layouts parse" ok "(skipped: no Python with PyYAML)"
        return
    fi

    local output
    output="$("$python" - <<'EOF'
import glob, yaml
bad = []
for path in glob.glob("assets_dev/engine/ui/**/*.yml", recursive=True):
    try:
        yaml.safe_load(open(path, encoding="utf-8"))
    except Exception as error:
        bad.append(path + ": " + str(error).splitlines()[0])
print("; ".join(bad) if bad else "ok")
EOF
)"

    if [ "$output" = "ok" ]; then
        report "menu layouts parse" ok ""
    else
        report "menu layouts parse" FAILED "$output"
    fi

    # Every option of the settings catalog has its row on its page (ui/gameplay/settings_<section>.yml: Label,
    # Description and Control): the settings window ends the game on one that has none (2026-10-16, two new options).
    output="$("$python" - <<'EOF'
import yaml
catalog = yaml.safe_load(open("assets_dev/engine/ui/menu/settings_catalog.yml", encoding="utf-8"))
def name(text):
    return "".join(part[:1].upper() + part[1:] for part in text.replace("-", "_").split("_") if part)
def element_ids(node, found):
    if isinstance(node, dict):
        if "id" in node:
            found.add(node["id"])
        for value in node.values():
            element_ids(value, found)
    elif isinstance(node, list):
        for value in node:
            element_ids(value, found)
    return found
ids = {}
missing = []
for setting in catalog["settings"]:
    section = setting["section"]
    if section not in ids:
        page = yaml.safe_load(open(f"assets_dev/engine/ui/gameplay/settings_{section}.yml", encoding="utf-8"))
        ids[section] = element_ids(page, set())
    for suffix in ("Label", "Description", "Control"):
        element = "Settings" + name(section) + name(setting["id"]) + suffix
        if element not in ids[section]:
            missing.append(element)
print("; ".join(missing) if missing else "ok")
EOF
)"

    if [ "$output" = "ok" ]; then
        report "settings options have their rows" ok ""
    else
        report "settings options have their rows" FAILED "missing $output"
    fi
}

maw_load() {
    local output
    output="$("$game" --headless-mod-report 5 2>/dev/null | grep -a 'files loaded')"

    case "$output" in
        *" 0 failed"*"; 0 distinct errors"*) report "MAW loads without errors (MAW)" ok "($output)" ;;
        *) report "MAW loads without errors (MAW)" FAILED "$output" ;;
    esac
}

# --- with the test mod only ------------------------------------------------------------------------------------------
export OPENYAMM_MODS_CONFIG="$tests/mods.test.yml"
ui_layouts
selftest "runtime self-test"

for path in "$tests"/scenarios/*.yml; do
    scenario "$(basename "$path" .yml) (test mod)" "$path"
done

unset OPENYAMM_MODS_CONFIG

# --- with mods off: the unmodded game must be unaffected -------------------------------------------------------------
export OPENYAMM_DISABLE_MODS=1
scenario "save_roundtrip (mods off, upstream save format)" "$tests/scenarios/save_roundtrip.yml"

if [ "$mode" != "quick" ]; then
    scenario "arena/mm8_challenge (mods off)" "tests/scenarios/arena/mm8_challenge.yml"
fi

unset OPENYAMM_DISABLE_MODS

# --- with the user's mods.yml (MAW) ----------------------------------------------------------------------------------
if [ -f mods.yml ]; then
    # The checks compare against numbers taken at MAW difficulty, whatever the user has set for play: they run on a
    # copy of mods.yml with the difficulty set to MAW. It sits beside mods.yml, as the paths in it are relative to it.
    checkConfig="mods.checks.yml"
    sed -E 's/^([[:space:]]*difficulty:).*/\1 MAW/' mods.yml > "$checkConfig"
    export OPENYAMM_MODS_CONFIG="$PWD/$checkConfig"

    # The files the scripts write (MAW's run-wide death counter, one line for every new game) go to a fresh folder,
    # not the player's mods/maw/files; the player's MAW settings are copied in, as the checks have always used them.
    dataDir="$root/$tests/logs/maw_files"
    rm -rf "$dataDir"
    mkdir -p "$dataDir"
    cp mods/maw/files/MAWSettings.lua "$dataDir/" 2>/dev/null
    export OPENYAMM_MOD_DATA_DIR="$dataDir"
    maw_load

    # The arena scenario is not run with MAW: the game picks arena monsters by their level in the monster table,
    # which MAW rewrites, and MAW has arena rules of its own that are not ported (see MAW_PORT_PLAN.md, M6).
    for name in bindings bindings_indoor; do
        scenario "$name (MAW)" "$tests/scenarios/$name.yml"
    done

    # Numbers read off a real install of the same MAW.
    for path in "$tests"/maw/*.yml; do
        scenario "$(basename "$path" .yml) (MAW)" "$path"
    done

    unset OPENYAMM_MODS_CONFIG OPENYAMM_MOD_DATA_DIR
    rm -f "$checkConfig"
else
    echo "mods.yml not found: MAW checks skipped"
fi

rm -f "$log"

# Scenario runs rewrite the tracked settings.ini; put it back.
git restore settings.ini 2>/dev/null

echo
if [ "$failed" -eq 0 ]; then
    echo "all checks passed"
else
    echo "$failed check(s) FAILED"
fi

exit "$failed"
