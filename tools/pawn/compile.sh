#!/bin/sh
# 5.02.04: compile a .pwn into a .amx that runs on open.mp servers AND on the MP server (--samp).
#   tools/pawn/compile.sh mymode.pwn [out.amx] [extra pawncc options]
# Include paths: omp-stdlib (#include <open.mp>), then YSI-Includes, amx_assembly, code-parse.inc,
# indirection, md-sort and third_party/omp-dev itself, so <YSI_Coding\y_hooks>, <amx_assembly\...> etc. resolve.
# -Z+ = compatibility mode: Windows "\\" include paths (used by YSI) work on Linux too.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd); D=$ROOT/third_party/omp-dev
[ $# -ge 1 ] || { echo "usage: $0 file.pwn [out.amx] [pawncc options]"; exit 1; }
IN=$1; shift; OUTF=${IN%.pwn}.amx
case "$1" in *.amx) OUTF=$1; shift;; esac
[ -x "$ROOT/tools/pawn/bin/pawncc" ] || sh "$ROOT/tools/pawn/build_pawncc.sh"
[ -f "$D/omp-stdlib/open.mp.inc" ] || git -C "$ROOT" submodule update --init --depth 1 third_party/omp-dev/omp-stdlib
INC=""; for i in omp-stdlib YSI-Includes amx_assembly code-parse.inc indirection md-sort; do [ -d "$D/$i" ] && INC="$INC -i$D/$i"; done
LD_LIBRARY_PATH=$ROOT/tools/pawn/bin "$ROOT/tools/pawn/bin/pawncc" $INC -i"$D" -Z+ -d0 -O1 '-;+' '-(+' "$@" "$IN" -o"$OUTF"
echo "built $OUTF (load it with: mp_server --samp $OUTF, or copy to an open.mp server's gamemodes/)"
