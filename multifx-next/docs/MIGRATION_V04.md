# Explicit v0.4 guitar preset migration

`tools/migrate_v04.py` converts **one preset object** from `multifx4/web_panel.py` into a schema 1 Next session. It never runs either host, loads a shared library, changes the source JSON, or activates the result. A named-preset collection must first be reduced to the chosen preset object. v0.4 preset saves do not always include MIDI mappings; only mappings actually present in the input are migrated.

```sh
python3 multifx-next/tools/migrate_v04.py /absolute/path/old-preset.json \
  --modules-dir /absolute/path/build-multifx2/modules \
  --plugins-dir /absolute/path/next-build/plugins \
  --ir-dir /absolute/path/old-irs \
  --output /absolute/path/new-session.json
```

The output directory must already exist. Source and output must differ. The command refuses an existing session output or CAB asset, including symlinks. It writes each JSON completely to a temporary file, fsyncs it, then publishes with an atomic no-clobber hard link. If publication raises an exception, files published by this invocation and its temporary files are removed. A multi-file output family is not a filesystem transaction: abrupt process termination or power loss can leave an orphan CAB asset. An orphan is never overwritten on retry. Filesystems must support hard links; the tool reports an error otherwise.

## Accepted input

Top-level fields are `chain`, `bpm`, `master`, and optional `cab`, `synth`, `midi`. Unknown fields, duplicate JSON keys, non-finite numbers, invalid types/ranges, duplicate channel/CC pairs, and inconsistent FX MIDI targets are errors. `bpm` (20–300), `master` (0–1), and `chain` (up to 16 entries) are required.

Each chain entry accepts `key`, `enabled`, optional `params`, optional `route` (historical default 0). `enabled` must be a JSON boolean. `key` is restricted to 1–48 lowercase ASCII letters, digits and underscores. Metadata comes from `<modules-dir>/<key>.json`; if absent, the tool reads the repository's actual `multifx2/manifests/<key>.json`. The manifest key must match, normalized defaults must be finite and in 0–1, and supplied parameter count must match the manifest. If `params` is absent, the manifest's `def` values are used. These are declared defaults, not measurements or guesses about a running instance. The library must be a readable file contained in `modules-dir`; metadata and asset symlink escapes are rejected.

A guitar entry (`route: 0`) becomes `legacy` 1.0.0, referencing `<plugins-dir>/legacy/plugin.so` and the absolute old `<key>.so` asset path. All 48 parameters are emitted as `p0` through `p47`: real parameter values followed by adapter padding of 0.5. Parameter ranges are normalized 0–1. `enabled: false` becomes `BYPASS`; other guitar entries become `ACTIVE`. Guitar order is preserved, and generated slot IDs are sequential. The source module files remain runtime dependencies; do not remove them after converting.

BassAmbient entries (`route: 1`) are excluded with individual warnings. Their settings and MIDI targets are excluded with warnings. No `synth` settings are carried into the output or treated as an instrument. Recognized synth values are type/range checked before exclusion; unknown synth fields are errors. This is a guitar-only migration, not a reconstruction of the old parallel synth bus.

## CAB and MIDI

The recognized CAB fields and historical defaults come from v0.4: `on: 0`, `mix: 1`, `blend: 0`, `level: 0.5`, `lowcut: 0.12`, `highcut: 0.85`, `ir0: ""`, `ir1: ""`. Numeric values must lie in 0–1. Nonempty IR names must be plain `.wav` filenames; the referenced readable file must resolve inside `ir-dir`. The migrator checks filesystem readability, not WAV decoding or DSP correctness.

When either IR is present, a final `cab_dual` 1.0.0 slot references `<plugins-dir>/cab_dual/plugin.so`. Its parameters follow the descriptor order: mix, blend, level, lowcut, highcut, enabled; latency is 64 frames. The sibling asset file `<output-stem>.cab.json` contains absolute `ir_a`, `ir_b` paths and `dry_mode`. Empty IR slots remain empty; one loaded slot is not duplicated. A full 16-slot guitar chain plus CAB is rejected. Without an IR no CAB slot is created, and any CAB MIDI mappings are explicitly reported as excluded.

The default is `dry_mode: "legacy"`, preserving the old unaligned dry-path behavior at partial mix. `--align-dry` chooses `"aligned"` and emits a warning because phase/timing at partial mix changes. Neither choice is a claim of sample-identical whole-session output; validate the converted preset with the actual binaries and assets.

Global `master` is `1.4 * old_master`, matching the old host's gain factor. The session is an automatic chain with empty explicit `audio_edges` and `midi_routes`. MIDI CC mappings use `midi_bindings` records with integer `target`, zero-based `param`, and explicit `min`/`max`:

- `master`: target 0, param 0, range 0–1.4.
- `bpm`: target -1, param 0, range 20–300.
- `fx`: map original chain position and matching key to the new guitar slot ID; preserve the real parameter index and range 0–1.
- `cab.mix`, `cab.blend`, `cab.level`, `cab.lowcut`, `cab.highcut`, `cab.on`: CAB slot target and parameter indices 0–5, range 0–1.

Only v0.4's supported synth MIDI kinds are recognized and explicitly excluded. Unsupported target kinds are errors. A mapping for an excluded route-1 effect is reported and dropped; subsequent guitar mapping positions are still resolved against the original chain, not the shortened output chain. Channels are 1–16 and CC numbers 0–127.

## Verification

```sh
python3 multifx-next/tests/migrate_v04_test.py
```

The CLI tests use real temporary source JSON, manifests and readable filesystem fixtures. The `.so` and `.wav` fixture files are deliberately **not DSP binaries or valid WAVs**: the migrator's contract only checks their paths/readability. Tests cover parameter/default conversion, bypass/master, two IR assets, dry mode, exclusions, position mapping, CAB CC indices, malformed inputs, missing files, symlink escapes, no-overwrite behavior, and cleanup after an injected publication failure. Runtime ABI validation, WAV decoding, rendered-audio comparison and hardware MIDI behavior require the host and real plugins/assets.

Sources used for the mapping: `multifx4/web_panel.py` (`DEFAULT_CAB`, `DEFAULT_SYNTH`, `MIDI_KIND`, catalog, clean_snapshot), `multifx4/host.cpp` (master gain and normalized MIDI transforms), `multifx-next/plugins/legacy.cpp`, `plugins/legacy.json`, `plugins/cab_dual.cpp`, and `src/session.cpp` schema encoding.
