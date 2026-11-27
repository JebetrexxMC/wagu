#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="${TMPDIR:-/tmp}/coopfusion_sync_interpolation_test"
CXX="${CXX:-c++}"
"$CXX" -std=c++17 -Wall -Wextra -Werror \
  "$ROOT/tests/host/sync_interpolation_test.cpp" -o "$BIN"
"$BIN"
rm -f "$BIN"
