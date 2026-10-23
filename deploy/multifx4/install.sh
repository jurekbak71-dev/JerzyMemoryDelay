#!/bin/sh
set -eu
[ "$(id -un)" = "jerzy" ] || { echo "Please run as jerzy"; exit 1; }
[ "$PWD" = "/home/jerzy/JerzyMultiFX4" ] || { echo "Run cd ~/JerzyMultiFX4 first"; exit 1; }
cmake -S . -B build-multifx2 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DJERZY_BUILD_PLUGIN=OFF -DJERZY_BUILD_JACK=OFF -DJERZY_BUILD_MULTIFX=OFF \
  -DJERZY_BUILD_MULTIFX2=ON -DJERZY_BUILD_MULTIFX4=ON
cmake --build build-multifx2 --parallel 2
ctest --test-dir build-multifx2 --output-on-failure
python3 multifx2/check_module_metadata.py build-multifx2/modules
python3 -m py_compile multifx4/web_panel.py
mkdir -p /home/jerzy/.config/jerzy-multifx4/irs
python3 multifx4/generate_demo_irs.py /home/jerzy/.config/jerzy-multifx4/irs
# Copy previous presets/chain if available. Never change v0.2 or v0.3 files.
for source in /home/jerzy/.config/jerzy-multifx3 /home/jerzy/.config/jerzy-multifx2; do
  if [ -f "$source/presets.json" ] && [ ! -f /home/jerzy/.config/jerzy-multifx4/presets.json ]; then
    cp "$source/presets.json" /home/jerzy/.config/jerzy-multifx4/presets.json
  fi
  if [ -f "$source/last_state.json" ] && [ ! -f /home/jerzy/.config/jerzy-multifx4/last_state.json ]; then
    cp "$source/last_state.json" /home/jerzy/.config/jerzy-multifx4/last_state.json
  fi
done
for service in jerzy-multifx4 jerzy-multifx4-panel jerzy-multifx4-routing jerzy-multifx4-midi; do
  sudo install -m 0644 "deploy/multifx4/$service.service" "/etc/systemd/system/$service.service"
done
sudo systemctl daemon-reload
echo "V0.4 STAGED. Your running MultiFX remains untouched."
echo "Activate when ready: sh deploy/multifx4/activate.sh"
