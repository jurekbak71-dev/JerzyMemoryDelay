# Jerzy MultiFX v0.4 — guitar + BassAmbient + IR + MIDI Learn

**Development/testing build. Do not overwrite the verified v0.2 SD card.**

## Architecture

```text
HeadRush Core guitar input
       |
  13-module guitar effects chain (up to 16 instances)
       |
  CAB/IR Loader (two mono/stereo WAV IRs, FFT convolution)
       |
       +--------------------------+
                                  |
Self-generating BassAmbient       |      (original BassAmbient DSP source)
   independent BASS Start/Stop    |
   independent AMBIENT Start/Stop |
   Bass style: Disco/Acid/Funk/Techno/Rock/Post-Punk
   Bass model: Acid 303 / Sub 808 / Classic Analog
   Ambient: Hold / Flow / Krell, major/minor/etc chords
   FREE BPM or shared BPM         |
       |                          |
       +--------------------------+
                    |
               STEREO MIXER
                    |
             HeadRush Core L/R
```

## CAB / IR Loader
- Fixed on **the end of the guitar path, before the shared mixer**; it never filters synth.
- Up to two impulse files loaded independently; adjustable A/B blend, Dry/Wet, Level, Low Cut and High Cut, bypass.
- WAV RIFF PCM 16/24/32-bit or float 32-bit, mono or stereo; 8 MB max upload.
- Source is resampled to JACK SR; up to 32768 samples per IR channel retained.
- Partitioned 128-point FFT, 64-sample block and **64 samples of added algorithmic latency** in wet CAB path.
- IR files persist under `~/.config/jerzy-multifx4/irs`; selected filenames save with presets and last state.

## BassAmbient
- Uses original DSP source from [JerzyBassAmbientVST3](https://github.com/jurekbak71-dev/JerzyBassAmbientVST3).
- Original synthesis models, filters, phase/circuit voices, sequencing and pad algorithms. Rock/Post-Punk phrase motifs are new.
- Independent BASS and PAD Start/Stop. Sync to shared BPM or FREE tempo.
- Bass model, scale, style, root C2–C7, density, pad chord, two tone engines, evolution, independent internal FX sends, levels and stereo pan.
- Synth is mixed after guitar CAB, bypassing guitar cabinet convolution.
- CPU work is skipped when both synth layers are off. Real Pi4 JACK XRUN testing still required.
- MIDI CC mappings can control transport switches and parameters; native MIDI note input for BassAmbient is **not yet implemented**. The instrument is autonomous without any MIDI.

## Touch UI
Panel `http://10.42.0.1:8092/` has **five primary tabs**: Łańcuch, Efekt, Bass/Amb, CAB/IR, Więcej. Under Więcej: Library, Presets, MIDI Learn, System.

- Large 52–60px buttons and 38px slider thumbs for iPhone/iPad.
- Named guitar FX parameters in accordion groups; no sprawling single screen.
- Musical root notes (C, C#, D…), named scales/FX/models and units (Hz, dB, BPM, seconds).
- Presets: chain + synth + CAB settings, including selected IR WAVs.

## MIDI Learn
- JACK MIDI input port: `JerzyMultiFX4:midi_in`; USB MIDI must be bridged/routed to it.
- Optional `a2jmidid -e` automatically started when already installed; routing service scans JACK MIDI outputs every three seconds, including hotplug.
- On GUI click **UCZ MIDI**, move a hardware knob/slider: latest incoming CC channel+number is bound to target.
- Targets include any effects parameter, master/BPM, synth Start/Stop/notes/model/scale and CAB parameters.
- Mappings saved in `~/.config/jerzy-multifx4/extra.json` and restored at startup.
- This is **MIDI CC Learn**, not yet full MIDI note performance/clock sync.
- Physical DNA Easy Control MIDI device and hardware connection still need testing; don't assume 4 banks behave in any specific way before measuring real CC messages.

## Installation — offline RPi4

Download the complete source ZIP on Windows with internet:

https://github.com/jurekbak71-dev/JerzyMemoryDelay/archive/refs/heads/feature/jerzy-multifx-v04-dual-ir-midi.zip

Connect Windows to the existing `Jerzy-MultiFX` hotspot and execute PowerShell:

```powershell
scp "C:\Users\jurek\Downloads\JerzyMemoryDelay-feature-jerzy-multifx-v04-dual-ir-midi.zip" jerzy@10.42.0.1:/home/jerzy/
ssh jerzy@10.42.0.1
```

On Pi:

```sh
cd ~
python3 -m zipfile -e JerzyMemoryDelay-feature-jerzy-multifx-v04-dual-ir-midi.zip .
mv JerzyMemoryDelay-feature-jerzy-multifx-v04-dual-ir-midi JerzyMultiFX4
cd ~/JerzyMultiFX4
sh deploy/multifx4/install.sh
```

**Staging does not stop any existing MultiFX service or change JACK / Wi-Fi.**
It compiles new host and original 13 modules, checks all DSP tests, and installs disabled v0.4 systemd units.

After successful staging / tests only:

```sh
sh deploy/multifx4/activate.sh
```

If v0.4 fails systemd readiness checks, script requests restoring previously active v0.2/v0.3 services. To manually roll back:

```sh
sh ~/JerzyMultiFX4/deploy/multifx4/rollback.sh
```

Original verified v0.2 panel remains at `http://10.42.0.1:8090/` after rollback.

## CI and known limitations

GitHub Actions compiles tests on Ubuntu ARM64 and x64, uploads runnable Linux ARM64 host + 13 `.so` modules + UI (the artifact is not a bootable card image).

Not yet confirmed on user's physical RPi4 + HeadRush Core:
- Sustained performance with heavy convolution + Atomic Cluster + pads
- XRUNs and latency
- Full v0.4 reboot/autostart
- Physical MIDI CC from DNA Easy Control routed to JACK
- Real iOS Safari rendering and multi-IR workflow

Do not declare physical release stable until these tests pass. Keep working v0.2 microSD unchanged, or make a clone before replacing anything.
