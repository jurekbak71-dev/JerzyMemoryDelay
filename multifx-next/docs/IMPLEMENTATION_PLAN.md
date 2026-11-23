# Jerzy MultiFX Next Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Build and test a real standalone stereo JACK2 modular host with FX and MIDI instruments.
**Architecture:** Immutable prepared DAG plans cross one atomic pending mailbox at block boundaries; an acknowledgement mailbox retires old plans on the control thread. Plugins have C ABI v3 and shared library ownership outside audio.
**Tech Stack:** C++20, JACK2, CMake, nlohmann/json 3.11.3 (MIT), Python standard library packaging.
**Spec:** ARCHITECTURE.md, approved by user 2026-11-17: “Zatwierdzam, START”.

## Global Constraints

- All changes in multifx-next/; original source branch/history untouched; no push/merge/release.
- Maximum 16 slots, 64 audio edges, 64 MIDI routes, 256 events/block; DAG only, 0/2 plugin inputs and 2 outputs.
- No allocation/deallocation/locks/I/O in audio. Controller owns all plans/instances/libraries.
- BYPASS FX dry aligned; instrument silent; BYPASS -> ACTIVE fresh instance; UNLOADED releases instance after acknowledgement.
- No BassAmbient; no GUI; no claim of measured Pi/HeadRush latency without hardware.

## Review Focus

1. Failed create/restore must leave live audio intact (Task 3 rollback test).
2. MIDI overflow must not leave held notes sounding (Task 2 panic test).
3. Shutdown with a queued plan must not free an instance still in process (Task 2 stress/shutdown test).
4. Graph reordering must reconnect chain edges, not just reorder computation (Task 3 noncommuting FX test).
5. Archive metadata can be malicious; traversal/link/oversize/wrong ELF must be rejected before dlopen (Task 3 package tests).

### Task 1: ABI and real plugins

Files: include/jfx.h, src/plugin.{hpp,cpp}, plugins/{gain,synth,cab,legacy}.cpp, CMakeLists.txt, tests/plugin_test.cpp.
Interfaces: Library::open(path), Instance::create(shared Library, sample_rate,max_frames,asset); descriptor jfx_entry_v3; block process with planar stereo, MIDI events and transport.
- [x] Write tests for stereo gain samples, MIDI event offsets/Note Off/CC/PC, invalid ABI and library files, CAB impulse and dry alignment; compile/run and observe missing implementation.
- [x] Implement versioned descriptor validation, RAII loader and plugins; compare CAB wet to existing mfx4 convolver without modifying it.
- [x] Build and run plugin suite; retain RED/GREEN evidence; commit.

### Task 2: Graph compiler and lifetime

Files: src/engine.{hpp,cpp}, tests/engine_test.cpp.
Interfaces: GraphSpec vector nodes/edges/routes -> Engine::publish(spec) -> token/busy; Engine::collect(); Engine::process(BlockView) noexcept. Diagnostics read via lock-free atomics.
- [x] Write tests for exact stereo passthrough, serial/parallel mixes, MIDI routing, delay compensation, bypass/unloaded, bounded swap stress, invalid graph/overflow/oversize and no operator new/delete on audio; observe RED.
- [x] Implement prepared bounded buffers, DAG validation, edge delays, mailbox handoff and acknowledgement reclamation; share instances through controller-only refcounts.
- [x] Run normal and sanitizer tests, commit.

### Task 3: Session, presets and packages

Files: src/session.{hpp,cpp}, tools/jfx_package.py, tests/{session_test.cpp,package_test.py}.
Interfaces: Session::command(line) serializes controller edits; save/load JSON atomically; package tool pack/install produces versioned manifest.json + plugin.so directory.
- [x] Write tests for add/remove/move/modes/parameters/restore transaction rollback, shared libraries/unload measured owned bytes; archive validation and bad files; observe RED.
- [x] Implement explicit graph routing and preset validation; installed plugin metadata/ELF/hashes; optional ABI2 bridge migration path without synth sections.
- [x] Run suites and commit.

### Task 4: JACK backend and integration

Files: src/jack_backend.{hpp,cpp}, src/main.cpp, tests/jack_probe.cpp, tests/jack_integration.py.
Interfaces: JackBackend owns client/ports and calls same Engine::process; CLI stdin control separate from callbacks; lifecycle close precedes Engine destruction.
- [x] Write integration that starts dummy JACK2, routes a real second JACK client through the host, verifies stereo and MIDI-generated output while editing slots; observe RED.
- [x] Implement stereo/MIDI/transport, xrun and shutdown counters, bounded buffers, diagnostics including RSS outside audio, clean stop.
- [x] Run dummy JACK integration, stress and error paths, commit.

### Task 5: Full verification, review and documentation

Files: README.md, docs/{ARCHITECTURE,TESTING,PLUGIN_FORMAT,IMPLEMENTATION_LEDGER}.md, docs/evidence/.
- [x] Run independent CMake build/CTest; existing project tests using offline plugin-disabled CMake where dependencies permit; ASan/UBSan/TSan separately.
- [x] Attempt ARM64 cross-build if toolchain available; document actual result without equating it to device execution.
- [x] Independent code review; fix important findings with reproducing tests, final verification.
- [x] Update build/run/package instructions, measured release diagnostics and limitations; local commit only.

## Pre-flight

Task 1 block ABI is consumed by Task 2 and JACK. Session Task 3 owns instance changes, not audio. JackBackend Task 4 must stop before Engine destructor. All shared interfaces agree with approved design.

Ruling: execute inline immediately — user authorized START; no further plan approval gate or per-task confirmation. Cost: implementation choices remain agent responsibility and are documented.
