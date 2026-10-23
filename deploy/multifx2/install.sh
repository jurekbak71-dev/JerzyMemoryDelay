#!/bin/sh
# Stage only. Preserve MultiFX v0.1 and the JACK/hotspot setup unchanged.
set -eu
[ "$(id -un)" = "jerzy" ] || { echo "Run as jerzy" >&2; exit 1; }
[ "$PWD" = "/home/jerzy/JerzyMultiFX2" ] || { echo "First: cd ~/JerzyMultiFX2" >&2; exit 1; }
cmake -S . -B build-multifx2 -G Ninja -DCMAKE_BUILD_TYPE=Release  -DJERZY_BUILD_PLUGIN=OFF -DJERZY_BUILD_JACK=OFF -DJERZY_BUILD_MULTIFX=OFF -DJERZY_BUILD_MULTIFX2=ON
cmake --build build-multifx2 --parallel 2
ctest --test-dir build-multifx2 --output-on-failure
python3 -m unittest discover -s multifx2 -p 'test_*.py' -v
sudo install -m 644 deploy/multifx2/jerzy-multifx2.service /etc/systemd/system/
sudo install -m 644 deploy/multifx2/jerzy-multifx2-panel.service /etc/systemd/system/
sudo install -m 644 deploy/multifx2/jerzy-multifx2-routing.service /etc/systemd/system/
sudo systemctl daemon-reload
printf '\nSTAGED: Existing Jerzy MultiFX 0.1 audio and panel have NOT been changed.\n'
printf 'Next step only after tests: sh deploy/multifx2/activate.sh\n'
