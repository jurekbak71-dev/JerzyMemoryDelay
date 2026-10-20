# Source this file before configuring, building or testing in the prepared cloud machine.
export PATH=/workspace/shared/jerzy-toolchain/bin:/workspace/shared/gui-sysroot/usr/bin:$PATH
export PKG_CONFIG_SYSROOT_DIR=/workspace/shared/gui-sysroot
export PKG_CONFIG_LIBDIR=/workspace/shared/gui-sysroot/usr/lib/x86_64-linux-gnu/pkgconfig:/workspace/shared/gui-sysroot/usr/share/pkgconfig:/usr/lib/x86_64-linux-gnu/pkgconfig:/usr/share/pkgconfig
export LIBRARY_PATH=/workspace/shared/gui-sysroot/usr/lib/x86_64-linux-gnu${LIBRARY_PATH:+:$LIBRARY_PATH}
export LD_LIBRARY_PATH=/workspace/shared/gui-sysroot/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export XDG_CACHE_HOME=/workspace/shared/cache
