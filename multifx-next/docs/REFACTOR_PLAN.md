# RT engine refactor implementation plan

> Implement inline, test each stage, use independent review before PR.

Goal: fulfill attached Nov23 requirements without changing historical products or Pi.
Architecture: reuse earlier explicitly-versioned JFX3 core from local6988a10 as new
side-by-side target. Its existing RCU pending/ack owns all plans/IR/instances on
controller. Add state ramps/retirement, complete dual CAB, explicit preset/CC migration,
benchmarks and CI. Preserve ABI2 binary layout and old sources.

- [x] Audit actual v0.4, source Git, Notion context, baseline4/4.
- [ ] Import existing Next implementation with source attribution; fresh baseline tests.
- [ ] Bound and test click-reduced transitions/retirement; no DSP after bypass, delayed unload.
- [ ] Complete dual CAB A/B, cached filters, mix/level, bypass work skipping, numeric tests.
- [ ] Migrate guitar presets and all13 ABI2 modules; preserve params/master/tempo/CC without synth.
- [ ] Add generator/error plugins and graph/CAB/param/start-stop stress; sanitizer runs.
- [ ] Benchmark modes/FFT; p95/p99 callback, RSS/PSS, XRUN and physical-loopback tool/scripts.
- [ ] Optional root build integration and GitHub Actions x64/ARM64 without deployment.
- [ ] Final review, logical commits, push only new branch and Draft PR to source branch.

Review focus: old-plan lifetime during ramp, directACTIVE->UNLOADED retention, MIDI stuck
notes, dual CAB missing-slot semantics, migration rejection instead of silent feature loss,
no fake hardware metrics. Optional tails remain explicitly bounded / disabled default.
