#!/bin/sh
set -eu
sudo -v
for service in jerzy-multifx4-midi jerzy-multifx4-routing jerzy-multifx4-panel jerzy-multifx4; do
  sudo systemctl disable --now "$service" >/dev/null 2>&1 || true
done
prev="v02"
if [ -r /home/jerzy/.config/jerzy-multifx4/previous-version ]; then
  prev="$(cat /home/jerzy/.config/jerzy-multifx4/previous-version)"
fi
base=jerzy-multifx2
port=8090
if [ "$prev" = "v03" ]; then base=jerzy-multifx3;port=8091; fi
sudo systemctl enable "$base" "$base-panel" "$base-routing" >/dev/null
sudo systemctl start --no-block "$base" "$base-panel" "$base-routing"
echo "Restored $base — http://10.42.0.1:$port/"
