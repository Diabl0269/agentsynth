# Test Patterns

The conventions every test in this repo follows. A test that ignores one of these either hangs,
opens real hardware, leaks state into the next test, or passes while the feature it claims to cover
is unreachable from a real gesture. [`test-layers.md`](test-layers.md) is the catalogue of what each
suite covers; this doc is how to write one.

## Headless and deterministic constraints

Every suite runs with no real audio device, no network and no sleeps. Two rules carry that:

- **A `HostMode::Standalone` engine must never have `initialise()` called on it in a test** — that
  opens real hardware. Use `HostMode::Hosted` wherever a real, initialised `AudioEngine` is
  required: a hosted engine never touches the device manager and never opens MIDI inputs. Standalone
  engines are only ever default- or value-constructed. The two tests that do exercise
  `initialise()` subclass the engine and override the `initialiseDevices` seam, which is the only
  part of it that touches hardware.
- **Clock the graph through `prepareForHost()` / `processHostBlock()`**, which funnel into the same
  private `renderNextBlock()` the standalone device callback uses — so what a hosted test asserts
  holds for both modes.

Waiting is always bounded and always fails on timeout: a `condition_variable` with a deadline, or
the bounded message-pump poll `AccountServiceTests` established. Never a sleep, and never
`stopThread(0)` — that force-kills via `pthread_cancel`, which aborts under glibc.

## Test the real mouse path

A test that calls a controller's drag API directly (`GraphEditor::beginConnectionDrag` /
`endConnectionDrag`, or a component's own action method) never touches
`mouseDown`/`mouseDrag`/`mouseUp` at all — and those real entry points have their own state
machine. Cover a gesture-driven feature by synthesizing `juce::MouseEvent`s into the real component
callbacks, not by calling the method the callback would have called.

**Why.** `ModuleComponent::mouseUp` gates on `e.getMouseDownPosition()`, which JUCE holds **fixed**
at the original press point for the whole gesture while `e.getPosition()` tracks the live cursor. A
synthetic `MouseEvent` that (wrongly) moves both together makes that gate miss the source jack and
silently no-ops the entire gesture — while a direct-API test of the same feature passes. The same
class of gap applies to any handler that reselects, retargets or changes focus before doing its
work.

`Tests/Macros/MacroPortRealMouseDragTests.cpp` is the template: it drives mouseDown → mouseDrag →
mouseUp on the real `ModuleComponent` callbacks with a correctly-held-fixed `mouseDownPosition`.
`Tests/UI/Graph/DragStateResetTests.cpp`'s `realMouseEvent()` is the shared idiom;
`Tests/Mixer/ChannelFlow/ChannelFlowAutoChannelTests.cpp` is the MIDI analogue, and
`Tests/UI/Settings/MeterColourStopsEditorTests.cpp` drives a Settings widget the same way. A
component built for this exposes small `…ForTest` accessors (selected index, handle bounds, swatch
bounds, handle count) so the test can aim a real press at a real target rather than a hardcoded
pixel.

**The one documented exception is a stock `juce::Slider`.** Hand-built mouse events fed to a stock
Slider reach into platform mouse-capture and cursor code that this repo's own `Component`
subclasses never touch, and hung the mixer fader suite in CI. `Tests/UI/Mixer/MixerFaderTests.cpp`
instead drives the bound `juce::AudioParameterFloat`'s
`beginChangeGesture()`/`setValueNotifyingHost()`/`endChangeGesture()` — the exact three calls
`juce::SliderParameterAttachment`'s internal `Slider::Listener` makes in response to a real drag —
so the undo bracket under test is exercised through the identical listener callback, without going
through Slider's native mouse path.

## Headless test seams

**`juce::PopupMenu` never runs in a test process.** A feature reached only through a menu item
therefore needs the menu's callback body exposed as a real, named method that both the async menu
callback and the test call. `MixerInsertList::moveRow()`/`removeRow()`/`addModule()`,
`MixerSendList::addSendTo()`/`removeRow()`/`togglePreFaderForRow()`/`retargetRow()`, and the
`ChannelFlow` suite's `applyAddTrackMenuChoice` are the established shape: the menu callback calls
the same method, so the two paths can never diverge. A component that owns such a list hands it to
tests through one accessor (`MixerColumnComponent::getInsertListForTest()`).

The same reasoning covers context menus that *are* worth driving end to end: `GraphEditor` and the
track header expose `setShowContextMenuHookForTest` / `setShowCanvasContextMenuHookForTest`, so a
test intercepts the menu the real right-click builds rather than skipping the build.

**`MainComponent::setEditSurfaceOverrideForTest`** is the seam for edit-surface routing.
`resolveEditSurface()` consults the override before any real focus check, so a test can pin which
surface Cmd+C/V/D/X/R act on without needing a real Desktop peer and real keyboard focus — neither
of which exists headless. See [`../timeline/focus.md`](../timeline/focus.md) for what the surfaces
mean.

## The FakeAudioIODevice pattern

`Tests/FakeAudioIODevice.h` is a test-local `juce::AudioIODevice` subclass that opens nothing,
**starts no thread** (`start()` ignores the callback it is handed) and reports fixed numbers — name
"Fake", type "Test", 48000 Hz / 512 samples, settable active input/output channel `BigInteger`s,
latency 64 in / 128 out. A test calls `engine.audioDeviceAboutToStart(&fake)` and
`engine.audioDeviceIOCallbackWithContext(...)` **by hand**, in the order a real device would, with
synthetic input arrays it can assert against — so there is exactly one thread and every block lands
where the test put it. The engine is `HostMode::Standalone` here (that is the mode with a device
callback) but `initialise()` is still never called on it.

**Extend this fake rather than writing a second one.**

## Writing an engine-level timeline test

Use `synth::OfflineTransportDriver` rather than hand-rolling a `processHostBlock` loop. Construct
the engine `Hosted`, `initialise()` it, wire whatever the patch needs — **before** the driver is
constructed, since its constructor calls `prepareForHost`, which prepares the nodes — then
`renderBlocks` / `renderToBeat` and assert on the returned buffer or on the `BlockTimeInfo` stream
the block callback hands you.

At 48000 Hz / 120 BPM one beat is exactly 24000 samples, which keeps expected sample counts
integers. The default patch may legitimately be silent with no MIDI input, so a non-silence
assertion needs a source that runs without MIDI (an `OscillatorModule` is a drone in mono mode)
patched to the graph's Audio Output node.

## Component snapshot smoke tests

`Component::createComponentSnapshot(bounds)` verifies that a component renders without crashing and
produces non-empty pixels, with no real display or window:

```cpp
comp.setSize(width, height);
auto img = comp.createComponentSnapshot({0, 0, width, height});
EXPECT_GT(img.getWidth(), 0);
```

`StatusBarTests::RendersNonEmptyImage` and `ThemeTests::StyledWidgetSmokeTest.*` are the reference
cases. The same call also renders a component to a PNG for visual inspection when a layout bug needs
eyes on it — `ModuleComponentLayoutTests.cpp`'s `AdsrCardRendersToPngForVisualInspection` and
`CurveEditorPaintTest`'s opt-in `CURVE_EDITOR_SNAPSHOT` dump are opt-in, env-gated versions of the
same pattern.

## AppProperties isolation

Tests that read or write `ApplicationProperties` use an isolated temporary directory so they cannot
contaminate the shared settings file across runs. `MainComponent` exposes
`getAppPropertiesForTest()` so a fixture can target a temp dir:

```cpp
// In test SetUp():
auto tmpDir = juce::File::createTempFile("").getParentDirectory()
                  .getChildFile(juce::Uuid().toDashedString());
tmpDir.createDirectory();
// Write a value then verify MainComponent reads it back:
comp.getAppPropertiesForTest().getUserSettings()->setValue("librarySidebarVisible", "0");
// TearDown(): reset to defaults, then tmpDir.deleteRecursively()
```

## Shared settings file reset guard

A headless `MainComponent` built through `newPatchForTest()` has no `getAppPropertiesForTest()`
seam — it hits the real, shared on-disk "Agent Synth" `ApplicationProperties` file, so a persisted
setting leaks into every other such test.

Two RAII shapes exist, both opening the file through `Tests/TestSettingsHelpers.h`'s
`synth::test::userSettingsTestOptions()` — the production `synth::userSettingsOptions()`, never a
re-hardcoded copy (FRO58 replaced seven byte-identical private copies with that header):

- **Save and restore** — `synth::test::PersistedKeysGuard` (same header) snapshots the named keys on
  construction and puts the developer's exact values back on destruction, including "the key did
  not exist". Use it whenever the test writes a key a developer legitimately has their own value
  for (snap division, panel visibility, detached window bounds).
- **Hard reset to the documented default** — `ChannelFlowTestFixture.h`'s
  `ChannelFlowTest::resetKeys()` and `BottomDockActiveTabResetGuard.h`, which `removeValue()` or
  fix only the keys that suite touches, in both constructor and destructor, so a prior crashed run
  cannot leak either. `BottomDockActiveTabResetGuardMDT` resets `"bottomDockActiveTab"` so a
  PNG-snapshot test's Mixer-tab switch cannot leak into a later test's "Timeline is the default"
  assumption.

This matters beyond one process: concurrent suites in sibling worktrees share that same settings
file, which is why [`local-ci.md`](local-ci.md#running-suites-in-parallel) says to serialise them.

## End-to-end workflow conventions

`Tests/App/E2EWorkflowTests.cpp` constructs a complete `MainComponent` per test. Four conventions
make that work:

- **Coordinate conversion** — components are nested inside `MainComponent`, so connection tests use
  `localPointToGlobal()` to convert port positions to screen coordinates for `endConnectionDrag()`.
- **Relative counts** — the default patch creates around 14 nodes, so every test asserts
  `initialCount + N`, never an absolute value.
- **Module lookup** — `findNewModule(name, initialNodeIDs)` finds only modules added after a
  snapshot, avoiding false matches with default-patch modules.
- **Preset loading** — call `editor().detachAllModuleComponents()` before
  `PresetManager::loadPreset()`, or the load is a use-after-free.

## Sanitizers

CI's sanitizer job builds with `-fsanitize=address` only, and it is label-gated (see
[`ci-pipeline.md`](ci-pipeline.md#jobs)). **There is no ThreadSanitizer anywhere in CI**, so a
change that touches a thread contract — a lock-free queue, an atomic, a pointer shared across the
audio, MIDI and message threads — is built and run locally with `-fsanitize=thread` by whoever
makes it. `Tests/MidiRemote/RemoteEngineThreadingTests.cpp` is the suite written to be run that way;
its contract is in [`../control/midi-remote.md`](../control/midi-remote.md#threading-the-mapping-table-crosses-threads).
