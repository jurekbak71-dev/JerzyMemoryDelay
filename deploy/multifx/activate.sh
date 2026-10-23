#!/bin/sh
# Deliberate manual cutover with automatic rollback on failure.
set -eu
[ -x /home/jerzy/JerzyMultiFX/build-multifx/JerzyMultiFXJACK ] || {
 echo "Run install.sh first" >&2; exit 1;
}
echo "Switching audio from Memory Delay to MultiFX (the audio will briefly stop)."
sudo -v
sudo systemctl stop jerzy-routing jerzy-panel jerzy-delay
sudo systemctl disable jerzy-routing jerzy-panel jerzy-delay >/dev/null
sudo systemctl enable jerzy-multifx jerzy-multifx-panel jerzy-multifx-routing >/dev/null
sudo systemctl start --no-block jerzy-multifx jerzy-multifx-panel jerzy-multifx-routing
n=0
while [ "$n" -lt 35 ]; do
  if systemctl is-active --quiet jerzy-multifx &&
     systemctl is-active --quiet jerzy-multifx-panel &&
     systemctl is-active --quiet jerzy-multifx-routing; then
    echo "MULTIFX ACTIVE: http://10.42.0.1:8089/"
    echo "JACK and hotspot were NOT modified."
    exit 0
  fi
  sleep 1
  n=$((n+1))
done
echo "MultiFX services did not all start; rolling back to Memory Delay" >&2
sudo systemctl disable --now jerzy-multifx-routing jerzy-multifx-panel jerzy-multifx >/dev/null 2>&1 || true
sudo systemctl enable jerzy-delay jerzy-panel jerzy-routing >/dev/null
sudo systemctl start --no-block jerzy-delay jerzy-panel jerzy-routing
echo "Rollback requested. Check systemctl status and journalctl if needed." >&2
exit 1
