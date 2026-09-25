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

- **A test Rig whose `RemoteEngine` outlives its `AudioEngine`/graph segfaults at teardown if a MIDI
  knob gesture is still open.** The engine holds a bare pointer to the parameter of every gesture
  until 250 ms after its last event (`kGestureIdleMs`, driven by the Rig's fake clock). Its
  destructor ends whatever is still open. A Rig that declares `RemoteEngine remote;` *before* the
  engine destroys it *after* the graph. A test whose last CC didn't advance the clock past the idle
  window then calls `endChangeGesture()` on a freed parameter from `~RemoteEngine()`.

  Whether that segfaults depends on whether the freed block has been reused yet. So it can pass
  alone and crash after an unrelated test ran first. FRO137 hit exactly this and blamed an earlier
  `MainComponent` test, but the `MainComponent` only changed the heap layout. The fix is to call
  `remote.endAllGestures()` in the Rig's destructor or `TearDown()` (as
  `MidiRemoteWorkflowE2ETests.cpp`, `MidiLearnControllerTests.cpp` and
  `MidiRemotePanelTestFixture.h` do), or declare the `RemoteEngine` after the engine. The same bug
  existed in `~MainComponent()` itself; see
  [`midi-remote.md`](../control/midi-remote.md#how-does-a-hardware-value-reach-a-parameter).
- **A local macOS ASan build won't catch a use-after-free inside JUCE.** JUCE's modules compile as
  Objective-C++ (`.mm`) on macOS, and those take `CMAKE_OBJCXX_FLAGS`, not the
  `-DCMAKE_CXX_FLAGS=-fsanitize=address` the ASan recipe passes. The bad read above happened inside
  `juce::AudioProcessorParameter::endChangeGesture()` and passed silently under local ASan. CI's ASan
  job runs on Linux, where JUCE compiles as `.cpp` and is instrumented. Locally, also pass
  `-DCMAKE_OBJCXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer"`; with it, local ASan reports
  that same read. Add `-fsanitize-recover=address` to all three flag sets and run with
  `ASAN_OPTIONS=halt_on_error=0` to collect every report in one run instead of stopping at the first.

## Snapshot testing

`AudioRenderingTests` compares rendered audio against golden reference files in `Tests/reference/`.

- **Run**: `./build/Tests/Tests --gtest_filter="AudioRenderingTest.Snapshot*"`
- **Update the references** after a deliberate DSP change (a better filter algorithm, say):
  `bash scripts/update-reference.sh`
- **Listen**: `scripts/play-reference.sh <filename>` (requires `ffplay` or `aplay`).
