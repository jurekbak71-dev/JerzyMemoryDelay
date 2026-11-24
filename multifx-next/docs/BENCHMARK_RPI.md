# Reproducible Raspberry Pi / Core measurements

These tools prepare measurement; they do not claim Raspberry Pi, Core or physical round-trip results from an x86 development machine. No packages are installed. Python standard library only.

## Host telemetry

`benchmark_rpi.py` requires `--run` before launching the host/opening audio hardware. Without it, prints the exact launch command and exits. Example:

```sh
python3 tools/benchmark_rpi.py --host /absolute/path/JerzyMultiFXNextALSA --backend alsa --duration 1800 --preset /absolute/path/session.json --output active-30min.json --run
```

Defaults are Core `hw:CARD=Core,DEV=0`, 44100 Hz, period 64, periods 3, capture 6, playback 4. Actual backend configuration is recorded in raw STATS; requested settings are not proof of negotiation. JACK host connects to an externally configured server; the tool does not configure JACK rate/period/device. `--backend jack` passes no ALSA arguments.

A supplied session JSON is passed to `LOAD` without rewriting. Relative library/asset paths retain host working-directory semantics: use absolute paths for repeatability. Startup `--commands commands.json` is a JSON list of literal single-line control commands; for example `["MODE 1 BYPASS", "MODE 2 UNLOADED"]` after loading a session with those IDs. Commands and responses are retained. Use separate runs for ACTIVE, BYPASS and UNLOADED with the same session, physical connections, signal and duration. Record input source level, plugin versions, IR files/checksums and device routing separately. No built-in fictional presets.

Raw samples are streamed and flushed one at a time to `<output>.samples.jsonl`; the summary retains sample count, first/last metadata, and aggregate histograms, never the full history in RAM. Disk use grows with duration, while measurement-history memory remains bounded. Each raw sample includes STATS, monotonic time, /proc process CPU ticks, RSS, PSS (when accessible), thread ticks/names and unavailable reasons. CPU percentages use elapsed wall time and SC_CLK_TCK, normalized to one core: 100% means one fully occupied core, multicore usage may exceed 100%. Newly observed threads lack a previous tick value and yield null. PSS can be inaccessible. Host stderr is saved alongside JSON. Failures are saved in errors and yield exit status 1.

Histogram deltas use 4096 bins at 1000 ns width. Bin i gives [i*1000,(i+1)*1000) ns; last bin is censored at >=4,095,000 ns. p95/p99 report bounds, never invented point estimates. Overflow upper bounds and unavailable values are null with reasons; no interpolation or hardware-latency inference. Quantiles cover only intervals between telemetry snapshots, excluding startup. Raw counters and aggregate deltas include xruns/deadline misses when supplied by the backend. `dsp_histogram` and backend `callback_histogram` are analyzed separately. Raw and summary `timing_scope` identify scope: JACK full callback versus ALSA decode+DSP+encode, excluding wait/device transfer. Neither histogram is end-to-end I/O latency. Missing timing_scope is recorded with a reason.

Machine architecture, CPU info and device-tree model are recorded. An x86 machine is explicitly not a detected Raspberry Pi. Core identity verification is null: ALSA name alone does not verify actual attached hardware. Recommended physical runs: 30 minutes baseline/each mode, then 60 minutes of the intended chain. Preserve raw files and stderr. CPU governor, temperature/throttling, firmware/kernel, JACK settings and power supply should be recorded externally for reproducibility.

## Actual synchronous loopback

The benchmark does not capture ALSA/JACK audio. `round_trip` is null with a reason. Create a real stereo integer PCM16/24/32 WAV externally, using two synchronously captured channels sharing the same ADC clock:

1. Drive a short impulse or unambiguous broadband pulse. Split it: channel 1 records a direct timing reference; the other path traverses the engine's real input, selected chain, real output and physical cable to capture channel 2.
2. Ensure the reference is captured concurrently, with common-clock capture channels. Document DAC/reference placement: this determines which converter delays cancel. Avoid feedback and clipping; set safe hardware gains.
3. Keep a short event window (default <=262144 frames). Do not resample either channel independently. Capture enough silence on both sides for the full response.
4. Analyze with an explicit wiring assertion:

```sh
python3 tools/analyze_loopback.py actual-common-clock.wav --synchronous-reference --max-lag-frames 4410 --output actual-latency.json
```

Reports channel peak positions/difference and FFT cross-correlation lag over nonnegative lags; units are frames and milliseconds at the actual WAV rate. Correlation is normalized by full-record energy; low correlation, multiple repeated events and a maximum-lag boundary result need human inspection, not an automatic latency claim. Negative returns are outside correlation search; peak difference can flag reversed wiring. Broad filters/nonlinear chains can shift peaks and correlation differently. The tool measures channel-to-channel delay; call it engine round trip only when the documented signal path actually includes the engine.

`jack_iodelay` can separately characterize a physical JACK output-to-input cable using its own ports. That measurement excludes this engine unless the actual measured routing deliberately traverses it; do not report a bare jack_iodelay cable measurement as engine-chain latency. No automated hardware capture or physical patching was performed by these tools.

## Tests

`python3 tests/benchmark_tools_test.py`: quantile bound/overflow/empty cases and a generated PCM fixture with reference impulse at frame 3 and return at 10, 1000 Hz: expected 7 frames = 7 ms. This is a mathematical test fixture, not a hardware measurement. Missing synchronous-reference assertion must fail. The tests were first run before implementation and failed due to missing tools, then passed after implementation. A further fail-first streaming regression test rejected the previous in-memory samples list, then passed with JSONL streaming. When `NEXT_BENCHMARK_TEST_HOST` names the built ALSA host (set by CTest), that test runs the real host briefly with ALSA null, checks sample count/stream/metadata and a null round-trip value. This is explicitly x86/null telemetry validation, not audio-interface measurement. A separate 0.2 s null run captured 5 samples, timing_scope=alsa_decode_dsp_encode, 128 block delta, no errors and host exit 0; these values are diagnostic evidence only, not a performance benchmark.

## Offline mode comparison

`tests/mode_bench.cpp` uses actual Engine/Session and a caller-supplied plugin/asset.
It times 10000 blocks per state using process CPU clock, after transitions and128
warmup blocks, asserts no dormant slot calls, and records tracked capacity and reload
preparation plus8 blocks. Compile against the same Release next_core, for example:

```sh
c++ -std=c++20 -O2 -I multifx-next/src -I multifx-next/include -I multifx-next/vendor \
 multifx-next/tests/mode_bench.cpp BUILD/libnext_core.a -ldl -pthread -o mode_bench
./mode_bench BUILD/plugins/cab/plugin.so /absolute/path/long.wav
```

This excludes drivers, RT scheduling and USB. `std::clock` is process CPU time;
run the harness single-threaded and repeat on an idle system. Reload time includes
control preparation, publication,8 processed blocks and collection; it is not just
dl loading. Tracked capacities are estimates, not RSS or total third-party heap.
Native measurements and exact context are in REFACTOR_REPORT.md and evidence/refactor.
