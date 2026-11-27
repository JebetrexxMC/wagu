#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
cd "$ROOT"
g++ -std=c++17 -Wall -Wextra -Icoopfusion/world tests/host/test_aim.cpp -o "$OUT/test_aim"
"$OUT/test_aim"
python3 tests/host/test_aim_pose_source.py
g++ -std=c++17 -Wall -Wextra -Icoopfusion/world tests/host/test_cutscene_partner.cpp -o "$OUT/test_cutscene_partner"
"$OUT/test_cutscene_partner"
python3 tests/host/test_cutscene_partner_source.py
python3 tests/host/test_cutscene_mocap_source.py
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -DCOOP_CUTSCENE_MOCAP_TESTING \
    -Icoopfusion/world tests/host/test_cutscene_mocap.cpp \
    coopfusion/world/cutscene_mocap.cpp coopfusion/diagnostics/cutscene_anim_log.cpp \
    -o "$OUT/test_cutscene_mocap" -pthread
"$OUT/test_cutscene_mocap"
g++ -std=c++17 -O1 -DAML32 -DCOOPFUSION32 \
    -Itests/host/stubs -Icoopfusion/transport -Icoopfusion/world \
    -Icoopfusion/diagnostics -Icoopfusion -I. \
    tests/host/test_anim2_aim_roundtrip.cpp \
    coopfusion/transport/coop_client.cpp \
    coopfusion/transport/embedded_server.cpp \
    coopfusion/diagnostics/weapon_log.cpp \
    -o "$OUT/anim2_roundtrip" -pthread
timeout 60 "$OUT/anim2_roundtrip"
