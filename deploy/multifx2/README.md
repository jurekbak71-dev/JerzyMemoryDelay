# Jerzy MultiFX v0.2 — modular Raspberry Pi host

Separate development branch, separate Pi folder and independent systemd units. The working MultiFX v0.1 at http://10.42.0.1:8089/ stays unchanged until activation. Existing JACK / HeadRush Core audio and Wi-Fi hotspot configurations are not modified.

## New v0.2 functionality

- Modular ARM64 .so effects with stable C API in multifx2/api.h.
- "+ DODAJ FX" catalog in mobile panel; more than one copy, bypass, reorder and remove.
- Memory Delay is the ORIGINAL DSP wrapped as an independent module. All 14 original controls are present: Blend, Level, Feedback, Modulation Rate, Depth, Delay Time, BBD Model, LFO Wave, Circuit Bypass, Input Boost, Squelch, Modulation Enable, Tempo Sync, Note Division.
- Built-in Compressor, Overdrive, Tremolo and Memory Delay, each compiled as an independent .so file.
- Master, BPM/tap, named presets. Reads v0.1 presets (does not alter them).
- Automatically restores last chain from ~/.config/jerzy-multifx2/last_state.json after boot.
- New mobile panel http://10.42.0.1:8090/. Does not need Internet.
- Demonstration of runtime installation of one extra Lo-Fi Crush effect (no host recompile).

## Offline first-time install

Download the v0.2 GitHub branch ZIP on Windows and transfer it to Pi over Jerzy-MultiFX Wi-Fi:

    scp "C:\PATH\JerzyMemoryDelay-feature-jerzy-multifx-v02.zip" jerzy@10.42.0.1:/home/jerzy/

Then via SSH, with old MultiFX running:

    python3 -m zipfile -e ~/JerzyMemoryDelay-feature-jerzy-multifx-v02.zip ~
    mv ~/JerzyMemoryDelay-feature-jerzy-multifx-v02 ~/JerzyMultiFX2
    cd ~/JerzyMultiFX2
    sh deploy/multifx2/install.sh

This ONLY compiles, runs automated tests and stages disabled service definitions.
The existing 0.1 engine, panel and JACK keep running.

## First v0.2 activation

Only after tests succeed:

    cd ~/JerzyMultiFX2
    sh deploy/multifx2/activate.sh

The script disables/stops three v0.1 services, enables/starts three v0.2 services,
checks readiness and attempts rollback to v0.1 if any does not start.
It does NOT stop or modify JACK/hotspot.

New panel: http://10.42.0.1:8090/

To revert manually:

    sh ~/JerzyMultiFX2/deploy/multifx2/rollback.sh

Original v0.1 panel: http://10.42.0.1:8089/

Check status:

    systemctl is-active jerzy-jack jerzy-multifx2 jerzy-multifx2-panel jerzy-multifx2-routing

## Add a truly new effect WITHOUT rebuilding host

    cd ~/JerzyMultiFX2
    sh deploy/multifx2/install-example.sh

This compiles only Lo-Fi Crush and copies the .so and matching .json manifest
to build-multifx2/modules. In Safari, refresh the effects catalog, select
Lo-Fi Crush and press + DODAJ FX. No JACK restart.

To create more effects, implement JfxDescriptor and jfx_plugin in your own
C++ .cpp using multifx2/api.h, build an ARM64 shared library, provide
the same-name JSON manifest and copy both to build-multifx2/modules.
The host detects them on catalog refresh, no host compilation needed.
Only install trusted native code: .so files have full code-execution privileges.

Module API is per audio sample, max 16 simultaneously chained instances,
24 controls per effect. No allocation, locks, disk/network I/O inside process().
The .so interface is NOT a generic VST3/LV2 host. Existing VST3 effects
need a separate bridge or proper native port.

## Known limitations

Hardware CPU/XRUN, USB reconnect handling and switch noise still need Pi validation.
Changes to the module chain mid-song may cause short audio transitions.
Store the 0.1 folder and service files for rollback; never overwrite it.
The Wi-Fi-only HTTP panel has no login auth. Never port-forward it.
