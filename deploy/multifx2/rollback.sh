#!/bin/sh
# Return to the user's v0.1 audio chain and panel 8089, leaving JACK/hotspot alone.
set -eu
sudo -v
sudo systemctl disable --now jerzy-multifx2-routing jerzy-multifx2-panel jerzy-multifx2
sudo systemctl enable jerzy-multifx jerzy-multifx-panel jerzy-multifx-routing
sudo systemctl start --no-block jerzy-multifx jerzy-multifx-panel jerzy-multifx-routing
echo "Restoring Jerzy MultiFX v0.1 http://10.42.0.1:8089/"
