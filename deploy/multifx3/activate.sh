#!/bin/sh
# Safe cutover: do not modify JACK or Wi-Fi. Restore v0.2 if v0.3 fails.
set -eu
cd /home/jerzy/JerzyMultiFX3
[ -x build-multifx2/JerzyMultiFX2JACK ] || { echo "Build v0.3 first" >&2; exit 1; }
sudo -v
sudo systemctl stop jerzy-multifx2-routing jerzy-multifx2-panel jerzy-multifx2
sudo systemctl disable jerzy-multifx2-routing jerzy-multifx2-panel jerzy-multifx2 >/dev/null
sudo systemctl enable jerzy-multifx3 jerzy-multifx3-panel jerzy-multifx3-routing >/dev/null
sudo systemctl start --no-block jerzy-multifx3 jerzy-multifx3-panel jerzy-multifx3-routing
n=0
while [ "$n" -lt 40 ]; do
  if systemctl is-active --quiet jerzy-multifx3 &&
     systemctl is-active --quiet jerzy-multifx3-panel &&
     systemctl is-active --quiet jerzy-multifx3-routing; then
    echo "MULTIFX V0.3 ACTIVE http://10.42.0.1:8091/"
    exit 0
  fi
  sleep 1
  n=$((n+1))
done
echo "V0.3 startup check failed — rolling back to v0.2" >&2
sudo systemctl disable --now jerzy-multifx3-routing jerzy-multifx3-panel jerzy-multifx3 >/dev/null 2>&1 || true
sudo systemctl enable jerzy-multifx2 jerzy-multifx2-panel jerzy-multifx2-routing >/dev/null
sudo systemctl start --no-block jerzy-multifx2 jerzy-multifx2-panel jerzy-multifx2-routing
echo "Requested v0.2 restore: http://10.42.0.1:8090/" >&2
exit 1
