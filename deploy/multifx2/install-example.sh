#!/bin/sh
# Demonstrates addition of a brand-new effect without recompiling/restarting the host.
set -eu
cd /home/jerzy/JerzyMultiFX2
dest=build-multifx2/modules
[ -d "$dest" ] || { echo "First build v0.2 using install.sh" >&2; exit 1; }
c++ -std=c++17 -O3 -fPIC -shared -fvisibility=hidden \
 -I multifx2 -o "$dest/lofi_crush.so.tmp" multifx2/examples/lofi_crush.cpp
mv "$dest/lofi_crush.so.tmp" "$dest/lofi_crush.so"
cp multifx2/examples/lofi_crush.json "$dest/lofi_crush.json"
echo "Lo-Fi Crush installed. Refresh effect catalog in the v0.2 panel."
