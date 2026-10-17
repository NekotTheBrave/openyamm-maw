#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
exec ./tools/run_game.sh --isolated pfem-3d --world mm6 --map oute3.odm \
  --position -9728 -11319 161 --yaw-radians 4.758 --pitch-degrees -6.274 \
  --set debug.actor_models=true --set debug.actor_spawn_id=595 --set debug.actor_spawn_count=1 \
  --set debug.actor_spawn_x=-9888 --set debug.actor_spawn_y=-11719 --set debug.actor_spawn_z=161 \
  --set "debug.menu_input_tour_path=$PWD/tools/pfem_3d_spawn.yml" \
  --set debug.immortal=true --set video.shadows=true --warmup 0 --seconds 0 "$@"
