#!/bin/sh
set -eu

marker=/tmp/dlinject-demo.loaded
rm -f "$marker"
export DLINJECT_DEMO_FILE=$marker

/opt/dlinject/dlinject-target &
target_pid=$!
trap 'kill "$target_pid" 2>/dev/null || true; wait "$target_pid" 2>/dev/null || true' EXIT
sleep 1

dlinject "$target_pid" /opt/dlinject/dlinject-example.so
cat "$marker"
