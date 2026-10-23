#!/bin/sh
# Restore the verified v0 Memory Delay service chain.
set -eu
sudo -v
sudo systemctl disable --now jerzy-multifx-routing jerzy-multifx-panel jerzy-multifx
sudo systemctl enable jerzy-delay jerzy-panel jerzy-routing
sudo systemctl start --no-block jerzy-delay jerzy-panel jerzy-routing
echo "Old delay chain requested. Old panel: http://10.42.0.1:8088/"
echo "Check: systemctl is-active jerzy-jack jerzy-delay jerzy-panel jerzy-routing"
