#!/usr/bin/env bash
# Builds every staged candidate (web/stage.py) to WebAssembly and copies the
# modules to web/dist/bots/, where serve.py reads them: bazel-bin follows
# whatever was built last, so it is no place to serve from.
#
#   bash web/build.sh
set -euo pipefail

cd "$(dirname "$0")/.."
FLAGS=(-c opt --config=web)
bazel build "${FLAGS[@]}" //web/bots:wasm
mkdir -p web/dist/bots
bazel cquery "${FLAGS[@]}" //web/bots:wasm --output=files 2>/dev/null |
  while read -r file; do cp -f "$file" web/dist/bots/; done
chmod u+w web/dist/bots/*
echo "$(ls web/dist/bots/*.wasm | wc -l) module(s) in web/dist/bots"
