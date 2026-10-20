#!/usr/bin/env bash
# Local, non-root Debian cloud toolchain. Package signatures and HTTPS remain enabled.
set -euo pipefail
shared=/workspace/shared
sysroot="$shared/gui-sysroot"
mkdir -p "$shared/apt-state/lists/partial" "$shared/apt-cache/archives/partial" "$sysroot" "$shared/cache"
python3 -m venv "$shared/jerzy-toolchain"
"$shared/jerzy-toolchain/bin/python" -m pip install --no-cache-dir cmake==3.31.6 ninja==1.11.1.4
cat > "$shared/apt-state/sources.list" <<'EOF'
deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian trixie main
deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://security.debian.org/debian-security trixie-security main
EOF
apt_options=(-o "Dir::Etc::sourcelist=$shared/apt-state/sources.list" -o Dir::Etc::sourceparts=- -o "Dir::State::lists=$shared/apt-state/lists" -o "Dir::Cache=$shared/apt-cache" -o Debug::NoLocking=1)
/usr/bin/apt-get "${apt_options[@]}" update
/usr/bin/apt-get "${apt_options[@]}" --download-only --no-install-recommends -y install libx11-dev libx11-xcb-dev libxcb-util-dev libxcb-cursor-dev libxcb-keysyms1-dev libxcb-xkb-dev libxkbcommon-dev libxkbcommon-x11-dev libglib2.0-dev libcairo2-dev libpango1.0-dev libfontconfig1-dev libfreetype-dev libwayland-dev wayland-protocols xvfb xauth libxtst6 fonts-dejavu-core
python3 - <<'PY'
from pathlib import Path
import subprocess
shared=Path('/workspace/shared'); root=shared/'gui-sysroot'
for package in (shared/'apt-cache/archives').glob('*.deb'):
    if 'mingw' not in package.name:
        subprocess.run(['dpkg-deb','-x',str(package),str(root)],check=True)
# Development symlinks refer to runtimes already installed on the cloud image.
for path in root.rglob('*'):
    if path.is_symlink() and not path.exists():
        runtime=Path('/usr/lib/x86_64-linux-gnu')/path.readlink().name
        if runtime.exists():
            path.unlink(); path.symlink_to(runtime.resolve())
PY
sdk_commit=3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96
if [[ ! -d "$shared/vst3sdk" ]]; then
  git clone --no-checkout --filter=blob:none https://github.com/steinbergmedia/vst3sdk.git "$shared/vst3sdk"
  git -C "$shared/vst3sdk" checkout --detach "$sdk_commit"
  git -C "$shared/vst3sdk" submodule update --init --recursive --depth 1
fi
[[ "$(git -C "$shared/vst3sdk" rev-parse HEAD)" == "$sdk_commit" ]] || { echo 'Existing SDK has another revision; preserve it and choose a separate SDK path.' >&2; exit 1; }
"$shared/jerzy-toolchain/bin/cmake" --version
"$shared/jerzy-toolchain/bin/ninja" --version
g++ --version
