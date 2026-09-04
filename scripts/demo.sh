#!/bin/sh
set -eu

build_dir=${1:-build}
marker=$(mktemp)
rm -f "$marker"
export DLINJECT_DEMO_FILE=$marker

"$build_dir/dlinject-target" &
target_pid=$!
trap 'kill "$target_pid" 2>/dev/null || true; wait "$target_pid" 2>/dev/null || true; rm -f "$marker"' EXIT
sleep 1

"$build_dir/dlinject" "$target_pid" "$build_dir/dlinject-example.so"
test -s "$marker"
cat "$marker"
