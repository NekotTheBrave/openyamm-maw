#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
# Copy the ordinary settings; change only presentation and the requested resolution.
exec ./tools/run_game.sh --isolated sorpigal-demon-crowd --world mm6 --map oute3.odm \
  --position -9728 -11319 161 --yaw-radians 1.57079632679 --pitch-degrees -8.594 \
  --set video.resolution=1600x900 --set debug.actor_models=true \
  --set debug.actor_models_manifest=worlds/mm6/models/sorpigal_demon_crowd.yml \
  --warmup 10 --seconds 30 "$@"
