#!/usr/bin/env bash
# New Sorpigal grass: three Goblin A plus one Shaman (B) and King (C) as 3D models, immortal party.
set -euo pipefail
cd "$(dirname "$0")/.."
exec ./tools/run_game.sh --isolated gob-3d --world mm6 --map oute3.odm \
  --position -9728 -11319 161 --yaw-radians 4.758 --pitch-degrees -6.274 \
  --set debug.actor_models=true --set debug.actor_spawn_id=550 --set debug.actor_spawn_count=3 \
  --set debug.actor_spawn_x=-9728 --set debug.actor_spawn_y=-11919 --set debug.actor_spawn_z=161 \
  --set "debug.menu_input_tour_path=$PWD/tools/gob_3d_spawn.yml" \
  --set debug.immortal=true --set video.shadows=true --warmup 0 --seconds 0 "$@"
