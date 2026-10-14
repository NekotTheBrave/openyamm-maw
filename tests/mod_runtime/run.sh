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

    unset OPENYAMM_MODS_CONFIG
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
