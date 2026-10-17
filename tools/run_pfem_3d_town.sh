#!/usr/bin/env bash
# New Sorpigal with the female (PeasantF1) and male (PeasantM1) peasants shown as 3D models; private save and settings.
set -euo pipefail
cd "$(dirname "$0")/.."
exec ./tools/run_game.sh --isolated pfem-3d-town --world mm6 --map oute3.odm \
  --position -10104 -8900 161 --yaw-radians 1.5708 --pitch-degrees -4 \
  --set debug.actor_models=true --set debug.immortal=true --set video.shadows=true \
  --warmup 0 --seconds 0 "$@"
