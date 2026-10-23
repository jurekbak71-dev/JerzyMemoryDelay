# Jerzy MultiFX v0.3 — effects library + touch panel

**Development branch; the tested v0.2 on Raspberry Pi is NOT modified until deliberate installation.**

## Confirmed source scope

Original author DSP from these GitHub repositories was ported to a modular JACK effect:
JerzySmallCloneChorus, JerzyBigMuffCollectionVST3 (six models from MuffDSP.h),
JerzyAttackDecayVST3 (mono/poly DSP), JerzyTapeDriveVST3, JerzyMXAnalogDelayVST3,
JerzyReelEchoVST3, JerzyAtomicClusterVST3 and JerzyOilDrumEcho.
Plus the earlier Memory Delay and generic Compressor, Overdrive, Tremolo, Lo-Fi Crush.

**13 independently built native effect modules** are delivered:

| .so module | DSP origin | Main controls |
|---|---|---|
| memory_delay | JerzyMemoryDelay | All 14 original controls |
| compressor | MultiFX v0.2 | Threshold, Ratio, Makeup |
| overdrive | MultiFX v0.2 | Gain, Tone, Output |
| tremolo | MultiFX v0.2 | Rate, Depth |
| lofi_crush | MultiFX v0.2 example | Bits, Rate, Mix, Tone, Drive |
| small_clone | JerzySmallCloneChorus | Small Clone / Mistress BBD family |
| big_muff | JerzyBigMuffCollectionVST3 | Six original DSP voicings |
| attack_decay | JerzyAttackDecayVST3 | Attack, Decay, trigger mode, Harmonic fuzz |
| tape_drive | JerzyTapeDriveVST3 | 18 tape/preamp controls |
| mx_analog_delay | JerzyMXAnalogDelayVST3 | Delay A/B, models and wet FX subset |
| reel_echo | JerzyReelEchoVST3 | Full 39-control engine |
| atomic_cluster | JerzyAtomicClusterVST3 | Experimental spectral delay |
| oil_drum_echo | JerzyOilDrumEcho | 46 most-used controls of original ~80 |

**Not included as native RPi effects yet:** JerzyAutoTuneVST3,
JerzyAudioQuantizer, JerzyMonoAnalogVST3 internal JUCE FX, third-party
VST3 plugins, and Korg Drumlogue native units. Their formats/internals
need additional native ports or different hosts. MX Analog Delay's
many per-algorithm settings and Oil Drum Echo's less-used parameters
also need expanded ABI/panel work. Do not imply these are full VST3
binary compatibility. The actual selected DSP engines are reused.

## GUI (touch-first)

Separate tabs:
1. Chain — reorder with big up/down, bypass, edit and master/tempo
2. Effect — single effect at a time, grouped parameters and large sliders/switches
3. Library — catalog of installed modules, + ADD
4. Presets — load/save/delete
5. System — connection status and network information

Touch targets roughly 52–60 CSS px tall. Responsive CSS supports
iPhone, iPad and desktop. This is the panel served on port **8091**.

## Verify downloaded ARM64 binary package

The workflow `.github/workflows/multifx2-ci.yml` compiles a separate
module library on Linux ARM64 and uploads an Actions artifact named
`jerzy-multifx-modules-ubuntu-24.04-arm`.
This artifact is a ZIP containing a `.tar.gz` of
`JerzyMultiFX-Modules/*.so` and `*.json`.
It is a **module package only**, not the whole app/panel.
Do **not** install an x86_64 `.so` on Raspberry Pi.

The CI runs original MemoryDelay DSP tests, a C++ shared-module audio test,
Python panel API tests, and compares native parameter metadata to
the JSON UI manifests (counts, names, defaults).

## Offline setup on the Raspberry Pi

The existing RPi has no internet when connected to the Jerzy-MultiFX hotspot.
Using Windows internet, download the source branch ZIP:

https://github.com/jurekbak71-dev/JerzyMemoryDelay/archive/refs/heads/feature/jerzy-multifx-v03-library-ui.zip

Switch Windows Wi-Fi to **Jerzy-MultiFX** and copy the ZIP:

    scp "C:\Users\jurek\Downloads\JerzyMemoryDelay-feature-jerzy-multifx-v03-library-ui.zip" jerzy@10.42.0.1:/home/jerzy/

Open SSH:

    ssh jerzy@10.42.0.1

On Pi:

    cd ~
    python3 -m zipfile -e JerzyMemoryDelay-feature-jerzy-multifx-v03-library-ui.zip .
    mv JerzyMemoryDelay-feature-jerzy-multifx-v03-library-ui JerzyMultiFX3
    cd ~/JerzyMultiFX3
    sh deploy/multifx3/install.sh

This **only** builds and runs tests, installs disabled systemd services
and copies v0.2 presets/last chain into a new v0.3 data directory.
It preserves v0.2 audio and panel. If compilation/tests fail, STOP.

After successful staging:

    sh deploy/multifx3/activate.sh

This stops v0.2 services, starts new engine and UI, and requests rollback
to v0.2 if service readiness fails. It leaves `jerzy-jack` and the Wi-Fi
hotspot unchanged.

New touch panel: http://10.42.0.1:8091/

Manually return to v0.2:

    sh ~/JerzyMultiFX3/deploy/multifx3/rollback.sh

Old, verified panel: http://10.42.0.1:8090/

v0.2 is already reported tested to automatically start after a full RPi
reboot with HeadRush Core connected. **Repeat the full reboot test for
v0.3 once activated**, since it installs different systemd units.

## Architecture/safety

All modules are standalone trusted aarch64 Linux .so files loaded via
the JFX v2 C ABI. Engine and touch panel persist last state across
restarts in `~/.config/jerzy-multifx3/`.
The audio thread does not do file/network operations and no DSP module
is loaded inside the JACK callback.

Caveats: JACK runs at 64-frame period; CPU, latency and XRUN must be
measured on RPi 4B with Heavy FX enabled. In particular, spectral
Atomic Cluster, multi-machine MX Analog Delay and reverb/tape effects
can overload a busy RPi. Do not run the complete 13-effect chain at
full load without hardware soak tests.
