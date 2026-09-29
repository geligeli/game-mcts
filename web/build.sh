#!/usr/bin/env bash
# Builds staged candidates (web/stage.py) to WebAssembly and copies the modules
# to web/dist/bots/, where serve.py reads them: bazel-bin follows whatever was
# built last, so it is no place to serve from.
#
#   bash web/build.sh               every staged candidate
#   bash web/build.sh opus-v07 ...  just these
set -euo pipefail

cd "$(dirname "$0")/.."
FLAGS=(-c opt --config=web)
targets=(//web/bots:wasm)
if (($#)); then
  targets=("${@/#/\/\/web\/bots:}")
  targets=("${targets[@]/%/_wasm}")
fi
bazel build "${FLAGS[@]}" "${targets[@]}"
mkdir -p web/dist/bots
for target in "${targets[@]}"; do
  bazel cquery "${FLAGS[@]}" "$target" --output=files 2>/dev/null |
    while read -r file; do cp -f "$file" web/dist/bots/; done
done
chmod u+w web/dist/bots/*
echo "$(ls web/dist/bots/*.wasm | wc -l) module(s) in web/dist/bots"
