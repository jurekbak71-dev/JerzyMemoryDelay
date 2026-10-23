#!/bin/sh
# Switch from tested v0.1. Roll back services if new engine/panel/routing fail.
set -eu
cd /home/jerzy/JerzyMultiFX2
[ -x build-multifx2/JerzyMultiFX2JACK ] || { echo "Install v0.2 first" >&2; exit 1; }
sudo -v
echo "Stopping v0.1 audio temporarily. JACK and hotspot remain unchanged."
sudo systemctl stop jerzy-multifx-routing jerzy-multifx-panel jerzy-multifx
sudo systemctl disable jerzy-multifx-routing jerzy-multifx-panel jerzy-multifx >/dev/null
sudo systemctl enable jerzy-multifx2 jerzy-multifx2-panel jerzy-multifx2-routing >/dev/null
sudo systemctl start --no-block jerzy-multifx2 jerzy-multifx2-panel jerzy-multifx2-routing
n=0
while [ "$n" -lt 35 ]; do
  if systemctl is-active --quiet jerzy-multifx2 &&
     systemctl is-active --quiet jerzy-multifx2-panel &&
     systemctl is-active --quiet jerzy-multifx2-routing; then
    echo "MULTIFX V0.2 ACTIVE http://10.42.0.1:8090/"
    exit 0
  fi
  sleep 1
  n=$((n+1))
done
echo "v0.2 startup failed; requesting rollback to v0.1" >&2
sudo systemctl disable --now jerzy-multifx2-routing jerzy-multifx2-panel jerzy-multifx2 >/dev/null 2>&1 || true
sudo systemctl enable jerzy-multifx jerzy-multifx-panel jerzy-multifx-routing >/dev/null
sudo systemctl start --no-block jerzy-multifx jerzy-multifx-panel jerzy-multifx-routing
echo "v0.1 restore requested. Panel http://10.42.0.1:8089/" >&2
exit 1
