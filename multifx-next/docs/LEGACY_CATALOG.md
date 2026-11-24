# Real ABI2 catalog validation through the legacy adapter

`tests/legacy_catalog_test.cpp` tests the **13 actual ABI2 shared libraries**, their adjacent source-derived JSON manifests, and the ABI3 `legacy` adapter. It does not replace modules with mocks or reconstruct their DSP. The fixed catalog is atomic_cluster, attack_decay, big_muff, compressor, lofi_crush, memory_delay, mx_analog_delay, oil_drum_echo, overdrive, reel_echo, small_clone, tape_drive and tremolo. An empty/incomplete module directory cannot pass.

## What is checked

For each module the test opens the actual `jfx_plugin` descriptor, verifies ABI2 identity, parameter keys/order/count and normalized JSON defaults against descriptor defaults (tolerance 0.00002). It prepares separate cold direct ABI2, unchanged-parameter ABI2 reference, and ABI3 adapter instances at 44,100 Hz with 64-frame adapter blocks. It then processes asymmetric stereo sinusoids, transients and silent gaps through three phases:

1. 8,192 frames at manifest defaults and 97 BPM.
2. 8,192 frames with every real normalized parameter changed to an alternating 0.17/0.83 pattern and 173 BPM.
3. 8,192 frames with manifest defaults restored and 97 BPM.

The direct and bridged paths receive identical parameters, BPM, input and histories. Every output sample must be written and finite, and must match within absolute 0.000002 plus relative 0.000002 tolerance. Padding parameters 0.5 complete the adapter's 48-value array. Parameter restoration here means restoring parameter values while maintaining the same DSP history, not restoring a binary DSP-state snapshot.

A second independent direct instance runs the same input/BPM history with unchanged parameters. The varied phase must measurably differ from this reference, so a fixture that fails to exercise actual parameter effects cannot pass. Direct DSP output must also measurably differ from dry passthrough. After destructing the instances and calling the library release path, a second cold cycle must reproduce the first sample trace within 0.000002. The test invokes real create/process/destroy and dlopen/dlclose paths; physical removal of a library's mapping by the system loader is not asserted.

## Allocation observation

If `jfx_test_rt`, `jfx_test_reset`, `jfx_test_allocations`, `jfx_test_frees` and `jfx_test_observe` are available through `RTLD_DEFAULT`, the test first calibrates the hooks with an actual C++ allocation/free. It enables the existing `rt_alloc_guard.cpp` interposition only around each adapter block call and the two direct ABI2 sample calls. A nonzero observed allocation or free causes failure. Instance preparation/destruction, JSON parsing, vectors and test diagnostics are outside the guarded regions.

Without hooks it prints `allocation_guard=UNAVAILABLE`; audio comparisons still execute, but that run supplies no allocation evidence. These glibc hooks and tested paths do not prove that arbitrary third-party DSP, future parameters, other platforms or every allocator mechanism are allocation-free.

## Reproduced run

The following run used actual libraries from `/workspace/work/rt-baseline/modules` and `/workspace/work/rt-next-base/plugins/legacy/plugin.so`, not the migration tool's filesystem fixtures:

```sh
g++ -std=c++17 -O2 -DNDEBUG \
  -I multifx-next/src -I multifx-next/include -I multifx-next/vendor \
  multifx-next/tests/legacy_catalog_test.cpp \
  /workspace/work/rt-next-base/libnext_core.a -ldl -pthread \
  -o /workspace/work/legacy_catalog_test

LD_PRELOAD=/workspace/work/rt-next-base/libnext_alloc_guard.so \
  /workspace/work/legacy_catalog_test \
  /workspace/work/rt-baseline/modules /workspace/work/rt-next-base/plugins
```

Observed result: **13 passed, zero failures**. A separate `-Wall -Wextra -Wpedantic` compilation completed with no diagnostics. Execution logs for this workspace are `/workspace/work/legacy-catalog-real.log`, `legacy-catalog-skip_dsp.log`, `legacy-catalog-ignore_parameters.log`, and `legacy-catalog-ignore_bpm.log` in the same directory. Each module processed 49,152 stereo frames across its two adapter cycles; all bridge/direct and cold-repeat maximum errors were exactly zero. The allocation guard calibrated successfully and recorded zero process-time allocations/frees across all three processing paths.

| Module | Maximum parameter-induced sample difference |
| --- | ---: |
| atomic_cluster | 0.585643 |
| attack_decay | 0.732171 |
| big_muff | 0.833566 |
| compressor | 0.120194 |
| lofi_crush | 0.559217 |
| memory_delay | 0.511267 |
| mx_analog_delay | 0.154804 |
| oil_drum_echo | 0.458777 |
| overdrive | 0.440770 |
| reel_echo | 0.0800044 |
| small_clone | 0.202189 |
| tape_drive | 5.63613 |
| tremolo | 0.163149 |

These are test-stimulus differences, not quality scores or output-level guarantees. The test does not clip high output values; it rejects non-finite samples.

No stochastic divergence appeared in the measured traces. Several source engines use pseudo-random processes, but their per-instance seeds are initialized deterministically: examples include `multifx2/vendor/oil_echo/Engine.h`, `vendor/reel/TapeEngine.{h,cpp}`, `vendor/tapedrive/tapedrive_dsp.h`, `vendor/atomic/Dsp.h` and `vendor/mxdelay/mxdelay_mechanics.h`. Equality here is observed for the specified cold states/stimulus; it is not a claim that all noise-generating DSP is universally deterministic.

## Negative checks and limitations

Temporary adapter mutations were compiled outside the repository solely to check the assertions. A dry-passthrough mutation failed all 13 modules. A mutation replacing every supplied parameter with 0.5 also failed all 13. A mutation replacing the transported BPM with 120 failed `atomic_cluster` (block 132) and `mx_analog_delay` (block 18), establishing that the stimulus catches lost tempo forwarding; the other modules did not expose that mutation in this trajectory. The original adapter source and module binaries were not changed. The test does not substitute those mutations into a successful catalog result.

This test covers one sample rate, one block size, the specified parameter trajectory and two short lifecycle cycles. It does not establish all parameter combinations, long-run stability, sanitizer cleanliness, realtime deadlines, live hot-swap safety, binary state serialization, hardware audio behavior, or all allocation APIs. The adapter's resource byte estimate remains an estimate: this test does not inventory the old DSP's allocations at preparation time.

Actual code sources: `multifx2/api.h`, module descriptors and matching JSON files, `multifx-next/plugins/legacy.cpp`, `src/plugin.{hpp,cpp}`, and `tests/rt_alloc_guard.cpp`.
