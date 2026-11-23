# CAB PFFFT and ARM64 continuation plan

User authorization: original architecture approved, START, then “kontynuuj”. No new design gate; this implements the already approved FFT/NEON investigation. Execute inline under executing-plans/TDD; independent review at end.

Base: c7c616e204d3532c8f997976325bb1757606b825. No push/merge/release. All product changes in multifx-next/.

- [x] Add failing tests for a prepared 128-point real FFT / 64-frame partitioned convolver: impulse, direct-FIR oracle, reference comparison, random signals, boundary IR lengths, variable block calls, invalid input and no audio allocations.
- [x] Vendor only required single-precision PFFFT source/header/SIMD dependencies from efc1dd2de6c3562fc464f905d37d9281f08a20dd after license review. Preserve license in source and CAB .jfx assets.
- [x] Implement prepared aligned spectra and work buffers, no process allocation, same overlap-save timing, scaling, sanitization and clipping. Integrate CAB; bump package version rather than overwrite installed 1.0.0.
- [x] Compare old/new convolver accuracy and reproducible Release CPU/RAM measurements; no Pi speedup claim from x86.
- [x] Extract official Debian cross-toolchain locally, cross-build all host/plugin targets ARM64; inspect ELF and NEON instructions. Distinguish compilation from hardware execution.
- [x] Full normal/ASan/UBSan/TSan/JACK regression, review fixes with reproductions, documentation/evidence, local commit.

Interfaces: nextfx::PartitionedConvolver::prepare(vector<float>), tick(float) noexcept, resource_bytes(); nextfx::CabinetIR::load(path,sr), tick(stereo), resource_bytes(). ABI unchanged. MIDI/graph lifecycle untouched.

Review focus: FFT normalization/real DC-Nyquist packing; over-aligned buffers and exception lifetime; latency 64 for arbitrary block boundaries; .jfx binary license retention; ARM64 dependencies/libc baseline and runtime limitations.

Ruling: keep historical IR loader and reference convolver untouched; WAV conversion/resampling matches original. Only plugin DSP path changes. Benchmark is offline algorithm timing, never round-trip.
