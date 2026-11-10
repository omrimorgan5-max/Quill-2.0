#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "Usage: bash bootstrap.sh <input.qsc> <output-binary>" >&2
    exit 1
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INPUT="$1"
OUTPUT="$2"
IR_FILE="$(mktemp --suffix=.ll)"
trap 'rm -f "$IR_FILE"' EXIT

"$ROOT/build.sh"
"$ROOT/bin/quill-linux" --typeCheck -o "$ROOT/bin/quill-bootstrap" "$ROOT/src/compiler/bootstrap.qsc"
"$ROOT/bin/quill-bootstrap" "$INPUT" "$IR_FILE"
if [[ ! -s "$IR_FILE" ]]; then
    echo "bootstrap compiler did not emit LLVM IR" >&2
    exit 1
fi
clang -Wno-override-module -O2 "$IR_FILE" -o "$OUTPUT"