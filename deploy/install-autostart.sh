#!/bin/bash
set -euo pipefail
if [[ $(id -un) != jerzy ]]; then echo "Run as user jerzy, not sudo." >&2; exit 1; fi
cd /home/jerzy/JerzyMemoryDelay
for cmd in jackd jack_lsp jack_connect cmake python3; do command -v "$cmd" >/dev/null || { echo "Missing $cmd" >&2; exit 1; }; done
cmake -S . -B build-rpi -G Ninja -DCMAKE_BUILD_TYPE=Release -DJERZY_BUILD_PLUGIN=OFF -DJERZY_BUILD_JACK=ON
cmake --build build-rpi --parallel 2
ctest --test-dir build-rpi --output-on-failure
sudo install -m 755 deploy/connect-delay.sh deploy/connect-delay.sh
for unit in jerzy-jack jerzy-delay jerzy-panel jerzy-routing; do
  sudo install -m 644 "deploy/systemd/$unit.service" "/etc/systemd/system/$unit.service"
done
sudo systemctl daemon-reload
echo "Stopping any manual JACK/delay/web_panel processes is required before activation."
echo "After stopping them, execute: sudo systemctl enable --now jerzy-jack jerzy-delay jerzy-panel jerzy-routing"
