#!/bin/sh
set -eu
sudo -v
sudo systemctl disable --now jerzy-multifx3-routing jerzy-multifx3-panel jerzy-multifx3
sudo systemctl enable jerzy-multifx2 jerzy-multifx2-panel jerzy-multifx2-routing
sudo systemctl start --no-block jerzy-multifx2 jerzy-multifx2-panel jerzy-multifx2-routing
echo "Previous v0.2 restored at http://10.42.0.1:8090/"
