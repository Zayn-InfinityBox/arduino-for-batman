#!/usr/bin/env bash
# Compiles and runs the host-side tests. No Arduino toolchain needed - the
# logic in aggregator.cpp is deliberately hardware-free.
#
# Two binaries are built: one against a synthetic map that exercises every
# corner of the frame layout, and one against the real map from input_map.h.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
sketch="$here/../BatmobileInputAggregator"
out="$here/.build"

mkdir -p "$out"

common=(-std=c++11 -Wall -Wextra -Wno-unused-const-variable
        -I "$here/shim" -I "$here" -I "$sketch")

echo "== synthetic map =="
g++ "${common[@]}" -DAGG_TEST_MAP='"test_map.h"' \
    -o "$out/test_aggregator" \
    "$here/test_aggregator.cpp" "$sketch/aggregator.cpp"
"$out/test_aggregator"

echo "== production map =="
g++ "${common[@]}" \
    -o "$out/test_production_map" \
    "$here/test_production_map.cpp" "$sketch/aggregator.cpp"
"$out/test_production_map"

# The rotaries are matched through POS(), so the same expectations must hold
# whichever way the SW4 turns out to encode a detent.
echo "== production map, rotaries as plain detent numbers =="
g++ "${common[@]}" -DROTARY_AS_BITMASK=0 \
    -o "$out/test_production_map_numeric" \
    "$here/test_production_map.cpp" "$sketch/aggregator.cpp"
"$out/test_production_map_numeric"
