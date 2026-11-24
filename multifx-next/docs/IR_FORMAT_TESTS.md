# Actual CAB WAV/response coverage

`tests/ir_formats_test.cpp` links the production `next_cab_dsp`/PFFFT libraries and calls `nextfx::CabinetIR::load` on actual generated WAV files. No DSP or parser replacement. Checks throw and remain active with NDEBUG.

24 decoding cases: PCM16, PCM24, PCM32 and IEEE float32; mono and stereo; source and target sample rates 44100, 48000, 96000 Hz. Taps contain exact binary fractions, negative full scale -1, distinct channel gains and signs. Mono replication, signed sample decoding, fixed 64-frame delay, complete impulse response and finite outputs are checked against hand-specified taps to 2e-5 absolute sample error. These cases use equal source/target rates: they do not claim cross-rate resampling coverage (the separate compatibility test covers rate pairs).

A seven-tap stereo filter has its 128-sample impulse response recovered after removing precisely 64 latency samples. The test computes independent complex direct DFTs at bins 1, 3, 7, 13, 29, 47 and 63 out of 128, comparing magnitude and phase together as complex absolute error. This exercises non-DC/non-Nyquist frequency response. A 2e-5 absolute tolerance matches the existing convolver/reference tolerance, allowing single-precision FFT arithmetic without relaxing to a relative error near spectral zeros. Measured maximum complex DFT error in the manual x86 run: 7.75485e-08. This is a fixture-specific observation, not a universal error bound.

A 32768-tap stereo PCM24 IR checks both initial and final nonzero taps (output samples 64 and 32831) and silence elsewhere, exercising the final partition. Malformed cases require production loading to throw: unsupported format code, invalid alignment, truncated payload, invalid bit depth, and NaN float sample.

Executed manually against existing implementation (no forced failure for behavior that already works):

```sh
c++ -std=c++17 -DNDEBUG -O2 -I multifx-next/src -I multifx-next/vendor/pffft/include/pffft multifx-next/tests/ir_formats_test.cpp /workspace/work/rt-next-base/libnext_cab_dsp.a /workspace/work/rt-next-base/libnext_pffft.a -o /workspace/work/ir_formats_test
/workspace/work/ir_formats_test
```

PASS: 24 format/channel/rate cases, selected-bin response, maximum-length final partition and five malformed/nonfinite rejections. No source fix was needed. Register a test executable linked with `next_cab_dsp`; no CMake changes are included here. Hardware performance, all malformed RIFF permutations, asynchronous loading and audible transition behavior are outside this test.
