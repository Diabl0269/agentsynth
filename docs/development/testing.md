# Testing

All tests use GoogleTest and run headless — no audio device, no GUI window, no network, no sleeps.
`./build/Tests/Tests` reports the authoritative suite and case counts.

```bash
# Build and run every test (ENABLE_TESTS defaults OFF -- pass it explicitly)
cmake -S . -B build -DENABLE_TESTS=ON
cmake --build build --target Tests
./build/Tests/Tests

# Run one suite
./build/Tests/Tests --gtest_filter="E2EWorkflow*"

# Coverage (threshold: 85%)
bash scripts/coverage.sh
```

[`test-layers.md`](test-layers.md) is the catalogue of what each suite covers.
[`test-patterns.md`](test-patterns.md) is the conventions every test here follows — read it before
writing one. To reproduce what CI will check without waiting on a CI round-trip, run
[`local-ci.md`](local-ci.md)'s `scripts/ci-local.sh`.

## Build flags

```bash
cmake -S . -B build
cmake --build build
```

- **`ENABLE_TESTS`** defaults `OFF`. Pass `-DENABLE_TESTS=ON` to generate the `Tests` target; by
  default a build skips them to save time.
- **`ENABLE_COVERAGE`** is a separate opt-in used by the Ubuntu CI job and `scripts/coverage.sh`.
- **`ENABLE_AI_HARNESS`** defaults `OFF` — see [ai-harnesses.md](ai-harnesses.md).
- **`ENABLE_PLUGIN`** defaults `ON`, so the plain `cmake --build build` above also builds
  `AgentSynthPlugin` — VST3 on every platform, plus AU on macOS — from the same
  `AudioEngine`/`MainComponent` code the standalone app uses (see
  [`../architecture/plugin-layer.md`](../architecture/plugin-layer.md#plugin-layer)). Disable it for
  a faster app-only local loop:

  ```bash
  cmake -S . -B build -DENABLE_PLUGIN=OFF
  cmake --build build
  ```

  PR CI does not override `ENABLE_PLUGIN`, so its default-`ON` build compiles the plugin as part of
  the normal `cmake --build build` step on all three platforms it runs (Ubuntu, macOS, Windows) — a
  shared-code change that silently breaks the plugin wrapper fails the same job as the app, with no
  separate opt-in required.

- **Generator**: a bare `cmake --build` defaults to Unix Makefiles (serial, no `-j`) unless Ninja is
  installed and `-G Ninja` is passed. CI always uses Ninja (`.github/workflows/ci.yml`), and
  `scripts/ci-local.sh` already picks it automatically and passes `--parallel`. For a quick manual
  build that matches CI's speed: `brew install ninja` once, then
  `cmake -S . -B build -G Ninja && cmake --build build`.

## Adding tests for a new module

1. **Unit tests** in `Tests/Modules/<ModuleName>Tests.cpp`, or `Tests/FX/` for an FX module — DSP
   output, parameter handling, edge cases. `Tests/` mirrors `Source/` by area; see
   [`file-size-guard.md`](file-size-guard.md#how-to-split-an-over-cap-file).
2. **End-to-end coverage** — add the module's name string to the `moduleTypes` array in
   `E2EWorkflowTest.DropAllModuleTypes_NoCrash`.
3. **Add the file to `Tests/CMakeLists.txt`.** A test file that no CMake target compiles passes
   locally by not existing.

Two tripwire suites then apply automatically: a new audio-output module must be classified in
`ModuleAdoptionTests.cpp`, and a new float parameter is swept by `AutomationZipperTest` without any
edit — both fail the build until the module is accounted for.

## Known flaky patterns

- **A `MainComponent` and a raw `GraphEditor` Rig alternating in the same process can crash a
  LATER, unrelated test.** Found while writing `Tests/App/E2EPluginCardWorkflowTests.cpp` (FRO137):
  constructing/destroying a `MainComponent` (any test, including the existing `E2EWorkflowTest`
  suite) and then, in a SEPARATE test, building a hand-wired `GraphEditor`/`ModuleComponent` rig
  the way `MidiLearnControllerTests.cpp`/`MidiRemoteWorkflowE2ETests.cpp` do — crashes the second
  test, reliably, whenever the two run back to back. Isolated (`--gtest_filter` down to just the
  Rig-based test) it passes every time; the crash needs a `MainComponent` to have existed earlier
  in the same process. Root cause not chased down (this doc entry is the flag, not the fix — a
  debugger session is warranted before touching it further); the working fix for this suite was to
  never construct a raw Rig at all here — every test uses one `MainComponent` per test, exactly
  like `E2EWorkflowTest` already does, including for the MIDI Learn / fake-CC portion (a fake
  device key straight into `AudioEngine::handleIncomingMidiMessageFromSource`, same seam
  `MidiRemoteWorkflowE2ETests.cpp` uses, works regardless of host mode). If a future suite hits the
  same crash, suspect this pattern first rather than the new code.

## Snapshot testing

`AudioRenderingTests` compares rendered audio against golden reference files in `Tests/reference/`.

- **Run**: `./build/Tests/Tests --gtest_filter="AudioRenderingTest.Snapshot*"`
- **Update the references** after a deliberate DSP change (a better filter algorithm, say):
  `bash scripts/update-reference.sh`
- **Listen**: `scripts/play-reference.sh <filename>` (requires `ffplay` or `aplay`).
