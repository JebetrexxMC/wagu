#!/bin/sh
# 5.02.04: build the open.mp Pawn compiler (third_party/omp-dev/compiler) for this machine.
# Output: tools/pawn/bin/pawncc + libpawnc.so. Needs cmake, make and a C compiler.
#   -std=gnu11            gcc 15 treats "constexpr" (a function in sc.h) as a keyword under C23
#   -DPAWN_CELL_SIZE=32   64-bit hosts would otherwise default to 64-bit cells (AMX magic 0xf1e1),
#                         which neither open.mp nor the MP server (third_party/pawn-amx) load
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SRC=$ROOT/third_party/omp-dev/compiler
[ -f "$SRC/source/compiler/CMakeLists.txt" ] || git -C "$ROOT" submodule update --init --depth 1 third_party/omp-dev/compiler
OUT=$ROOT/tools/pawn/bin; B=$ROOT/tools/pawn/build
mkdir -p "$B" "$OUT"
cmake -S "$SRC/source/compiler" -B "$B" -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
      -DCMAKE_C_FLAGS="-std=gnu11 -DPAWN_CELL_SIZE=32" >/dev/null
cmake --build "$B" --target pawncc -j"$(nproc 2>/dev/null || echo 2)"
cp "$B/pawncc" "$B"/libpawnc.* "$OUT/"
echo "pawncc ready: $OUT/pawncc"
