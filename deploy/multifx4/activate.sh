#!/bin/sh
set -eu
cd /home/jerzy/JerzyMultiFX4
[ -x build-multifx2/JerzyMultiFX4JACK ] || { echo "Stage v0.4 first"; exit 1; }
sudo -v
mkdir -p /home/jerzy/.config/jerzy-multifx4
prev="v02"
if systemctl is-active --quiet jerzy-multifx3; then prev="v03"; fi
echo "$prev" > /home/jerzy/.config/jerzy-multifx4/previous-version
for base in jerzy-multifx2 jerzy-multifx3; do
  sudo systemctl stop "$base-routing" "$base-panel" "$base" >/dev/null 2>&1 || true
  sudo systemctl disable "$base-routing" "$base-panel" "$base" >/dev/null 2>&1 || true
done
sudo systemctl enable jerzy-multifx4 jerzy-multifx4-panel jerzy-multifx4-routing jerzy-multifx4-midi >/dev/null
sudo systemctl start --no-block jerzy-multifx4 jerzy-multifx4-panel jerzy-multifx4-routing jerzy-multifx4-midi
i=0
while [ "$i" -lt 40 ]; do
  if systemctl is-active --quiet jerzy-multifx4 &&
     systemctl is-active --quiet jerzy-multifx4-panel &&
     systemctl is-active --quiet jerzy-multifx4-routing; then
    echo "V0.4 ACTIVE — http://10.42.0.1:8092/"
    exit 0
  fi
  sleep 1
  i=$((i+1))
done
echo "V0.4 startup failed; restoring previous MultiFX" >&2
sh deploy/multifx4/rollback.sh
exit 1
