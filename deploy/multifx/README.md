# Jerzy MultiFX v0.1 — Raspberry Pi 4B (development branch)

This is a **separate staging and migration path**, not a replacement for the
proven Memory Delay setup. Do not enable until the new build and tests pass.

## What's new
- One JACK audio client: mono in_1, stereo out_1/out_2.
- Four switchable, rearrangeable DSP stages: compressor, overdrive, tremolo,
  and **the original** Jerzy Memory Delay BBD DSP (source/dsp.h).
- Real-time-safe control: parameter snapshots read from atomics once per JACK
  block; no locks, disk I/O or sockets in the audio callback.
- Mobile touch panel on http://10.42.0.1:8089/ (not port 8088).
- Named presets stored at /home/jerzy/.config/jerzy-multifx/presets.json
  (on the Raspberry Pi, survive reboot). Save / load / delete.
- One process, no extra JACK server; designed for 44.1 kHz / 64-sample
  buffer, HeadRush Core capture_1 -> MultiFX -> playback_1/2.
- Hotspot configuration and JACK service are NOT changed.

**Status:** source is prepared; DSP/ALSA behavior, CPU usage, XRUNs, switching
noise and preset recovery must be tested on the actual Raspberry Pi.
Stage before migrating. Initial effects are prototype DSP; only Memory Delay
reuses the established production DSP.

## Prerequisites
- Existing tested jerzy-jack service remains active.
- On Pi, GitHub must be temporarily reachable to clone this development branch.
  The independent Jerzy-MultiFX hotspot normally has no internet; don't
  attempt git clone from that offline Wi-Fi alone.
- cmake, ninja-build, pkg-config, libjack-jackd2-dev, compiler present.

## Clone and stage (no sound interruption)
From Pi with temporary internet access:
    git clone --branch feature/jerzy-multifx-v01 --single-branch \
      https://github.com/jurekbak71-dev/JerzyMemoryDelay.git ~/JerzyMultiFX
    cd ~/JerzyMultiFX
    sh deploy/multifx/install.sh

Check CTest result. Existing Memory Delay remains active, with port 8088.

## Manual cutover, only after staging
    cd ~/JerzyMultiFX
    sh deploy/multifx/activate.sh

The script stops and disables old jerzy-delay, jerzy-panel,
jerzy-routing, enables/starts three new MultiFX services, polls their
active state, and requests rollback if startup fails. It leaves jerzy-jack
and NetworkManager unchanged.

New panel: http://10.42.0.1:8089/.
If you need the old panel / proven effects:
    sh ~/JerzyMultiFX/deploy/multifx/rollback.sh

In case of issues:
    systemctl is-active jerzy-jack jerzy-multifx jerzy-multifx-panel jerzy-multifx-routing
    sudo journalctl -u jerzy-multifx -u jerzy-multifx-panel -u jerzy-multifx-routing -n 60 --no-pager
    jack_lsp -c

## Security & scope
- HTTP listens on the hotspot-only IP 10.42.0.1, no cloud/internet service.
  Wi-Fi must remain WPA2 protected; no remote port forwarding.
- UDP control bound to 127.0.0.1:9810. Parameter update packets use:
  P index normalized, E effect-index 0-or-1, O 0 1 2 3, B bpm.
- The web panel stores presets persistently, but the current live settings
  reset to defaults after reboot until a preset is loaded. Automatic restore
  is not implemented in v0.1.
- This is not a plug-and-play generic VST3 host; adding further DSP stages
  needs code/module integration. Stage order changes may produce artifacts.
- The fixed mono input is HeadRush Core capture_1; stereo is synthesized by
  the chain. Do not assume all 6 capture and 4 playback channels are mapped.
