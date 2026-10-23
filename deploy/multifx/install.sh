#!/bin/sh
# Stage MultiFX. Does NOT stop or modify the currently working Memory Delay chain.
set -eu
[ "$(id -un)" = "jerzy" ] || { echo "Run as jerzy" >&2; exit 1; }
[ "$PWD" = "/home/jerzy/JerzyMultiFX" ] || { echo "First: cd ~/JerzyMultiFX" >&2; exit 1; }
cmake -S . -B build-multifx -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DJERZY_BUILD_PLUGIN=OFF -DJERZY_BUILD_JACK=OFF -DJERZY_BUILD_MULTIFX=ON
cmake --build build-multifx --parallel 2
ctest --test-dir build-multifx --output-on-failure
sudo install -m 644 deploy/multifx/jerzy-multifx.service /etc/systemd/system/
sudo install -m 644 deploy/multifx/jerzy-multifx-panel.service /etc/systemd/system/
sudo install -m 644 deploy/multifx/jerzy-multifx-routing.service /etc/systemd/system/
sudo systemctl daemon-reload
echo "STAGED. Existing Memory Delay is untouched."
echo "Do NOT start both JACK output chains concurrently."
echo "Read deploy/multifx/README.md; when ready run: sh deploy/multifx/activate.sh"
