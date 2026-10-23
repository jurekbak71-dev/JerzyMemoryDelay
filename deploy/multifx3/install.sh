#!/bin/sh
# Stage v0.3 without touching running v0.2 services or HeadRush / Wi-Fi setup.
set -eu
[ "$(id -un)" = "jerzy" ] || { echo "Run as jerzy" >&2; exit 1; }
[ "$PWD" = "/home/jerzy/JerzyMultiFX3" ] || { echo "First cd ~/JerzyMultiFX3" >&2; exit 1; }
cmake -S . -B build-multifx2 -G Ninja -DCMAKE_BUILD_TYPE=Release \
 -DJERZY_BUILD_PLUGIN=OFF -DJERZY_BUILD_JACK=OFF -DJERZY_BUILD_MULTIFX=OFF -DJERZY_BUILD_MULTIFX2=ON
cmake --build build-multifx2 --parallel 2
ctest --test-dir build-multifx2 --output-on-failure
python3 -m unittest discover -s multifx2 -p 'test_*.py' -v
python3 multifx2/check_module_metadata.py build-multifx2/modules

# Clone 0.2 user presets/chain once. Never overwrite v0.2 files.
mkdir -p /home/jerzy/.config/jerzy-multifx3
for name in presets.json last_state.json; do
  if [ -f "/home/jerzy/.config/jerzy-multifx2/$name" ] && [ ! -e "/home/jerzy/.config/jerzy-multifx3/$name" ]; then
    cp "/home/jerzy/.config/jerzy-multifx2/$name" "/home/jerzy/.config/jerzy-multifx3/$name"
  fi
done

sudo install -m 644 deploy/multifx3/jerzy-multifx3.service /etc/systemd/system/
sudo install -m 644 deploy/multifx3/jerzy-multifx3-panel.service /etc/systemd/system/
sudo install -m 644 deploy/multifx3/jerzy-multifx3-routing.service /etc/systemd/system/
sudo systemctl daemon-reload
echo "V0.3 STAGED — MultiFX v0.2 still running; no services switched."
echo "Activate after tests: sh deploy/multifx3/activate.sh"
