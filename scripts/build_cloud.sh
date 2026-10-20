#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/cloud_env.sh
cmake -S . -B build-gui -G Ninja -DCMAKE_BUILD_TYPE=Release -DVST3_SDK_ROOT=/workspace/shared/vst3sdk -DCMAKE_PREFIX_PATH=/workspace/shared/gui-sysroot/usr -DCMAKE_PROGRAM_PATH=/workspace/shared/gui-sysroot/usr/bin -DWAYLAND_PROTOCOLS_BASEDIR=/workspace/shared/gui-sysroot/usr/share/wayland-protocols
cmake --build build-gui --parallel 3
xvfb-run -a ctest --test-dir build-gui --output-on-failure
