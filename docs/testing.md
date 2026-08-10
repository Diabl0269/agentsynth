# Testing Guide

All tests use GoogleTest and run headless (no audio device, no GUI window). 1283 tests across 156 suites
(`./build/Tests/Tests` reports the authoritative count; the per-section totals below are approximate).

```bash
# Run all tests (ENABLE_TESTS defaults OFF — must be passed explicitly)
cmake -S . -B build -DENABLE_TESTS=ON
cmake --build build --target Tests
./build/Tests/Tests

# Run a specific suite
./build/Tests/Tests --gtest_filter="E2EWorkflow*"

# Check coverage (threshold: 85%)
bash scripts/coverage.sh
```

By default, builds skip tests to save time. Use the `-DENABLE_TESTS=ON` flag to enable them.

## Test Layers

### Audio Rendering Tests (~217 tests)

Headless DSP tests that render audio through individual modules and verify output characteristics — RMS levels, silence detection, frequency response, waveform accuracy.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| OscillatorTest | 11 | Waveform generation (sine, saw, square, triangle), MIDI response, tuning, frequency accuracy |
| FilterTest | 10 | Low-pass/high-pass filtering, cutoff/resonance parameters, frequency response across 7 filter types |
| ADSRTest | 10 | Attack/sustain/release shapes, retriggering, poly mode, parameter changes during playback |
| EnvelopeFollowerModuleTest | 28 | Peak/RMS detection accuracy (unit sine → 1/√2 in RMS), attack/release time-constant ordering, unipolar clamped output, Attack/Release/Sensitivity CV modulation, CV channels cleared on output, bypass/mute emitting no CV, port roles (Audio in / ModCV out), state round-trip, zero-channel/zero-sample/zero-sample-rate robustness |
| LFOModuleTest | 11 | LFO waveform output, rate modulation, sync behavior |
| VCAModuleTest | 5 | Gain application, envelope following, silence detection |
| AttenuverterModuleTest | 4 | CV signal attenuation, bipolar control, CV modulation |
| SampleHoldModuleTest | 48 | Port topology & modulation targets, Sample-mode rising-edge latch + hold across blocks, Track-mode follow/freeze, internal free-running clock and Rate CV, random source range, Level/Offset/Slew shaping + their CV inputs, **Schmitt-trigger threshold** (low-amplitude and negative-threshold gates, hysteresis rejects dither and in-gap dips, full release re-arms, Threshold CV), **trigger meter telemetry** (signed block peak, live on internal clock, armed state, capture count, reset on bypass/mute), `TriggerMeterComponent` static helpers (`valueToX` clamping, `needsRepaint` idle-gate) + paint smoke test, trigger/CV channel hygiene, bypass dry pass-through & mute silence, zero-length/zero-channel/single-sample buffers, zero sample rate, state round-trip |
| MacroControlModuleTest | 14 | Port model (16 channels, 8 visible by default, no MIDI, ModCV role), per-macro CV output, channels above the `Knobs` count silent, hidden knobs keep their values when the bank is re-grown, bipolar mapping, 20 ms smoothing, bypass/mute silence, zero-channel buffer; factory creation, `macroCount`+knob JSON round-trip, one macro fanned out to two destinations |
| FX module tests | 53 | Delay (passthrough, feedback), Distortion (clipping, drive), Reverb (room size), Chorus, Phaser, Compressor, Flanger, Limiter, Bitcrusher (downsampling, quantization, CV) |
| PitchShifterModuleTest | 22 | Pitch mode transposition ratios (spectral peak), Frequency mode SSB offset + sideband suppression, CV routing, feedback stability, state round-trip |
| SamplerModuleTest | 31 | Registration + port/parameter surface; WAV load (success, missing file, unreadable file, failed load keeps the previous sample, clear); Sample mode playback verified sample-exact against a ramp file at unity rate, at `pitch = +12` (2×), via MIDI note transpose, and with `start = 0.5`; monotonic anti-click fade-in; one-shot falls silent at the last frame vs loop keeps going; Granular mode produces bounded finite audio and stays silent with no sample loaded, including at max density × max grain size; gate precedence (free-run with nothing patched, trigger-CV latch silences a low gate, retrigger, a gate rising mid-block is not mistaken for an unpatched jack); bypass/mute clear; CV channels do not leak to the output; level CV sums with the parameter; zero-channel buffer is safe; `getExtraState`/`setExtraState` round trip, restored through `graphToJSON` → `applyJSONToGraph` on the trusted path and **dropped** on the untrusted path |
| SampleWaveformPeaks | 4 | `SampleWaveformComponent::computePeaks` — empty inputs, columns span the buffer and track min/max extremes, channels averaged (opposite phase cancels), more columns than frames |
| SampleWaveformPaint | 2 | Paints the empty state ("No sample loaded") and a loaded sample with a live playhead into a `juce::Image`; repeat `timerCallback()` with nothing changed is a no-op; zero-width component is safe |
| SamplerFormats | 2 | `getSupportedFormatWildcard()` is non-empty and includes `*.wav`; `isSupportedAudioFile` accepts wav/WAV/aiff and rejects .json/.txt/extensionless/directories (extension-only check, so drag-hover stays cheap) |
| Parametric EQ tests | 64 | `Tests/ParametricEQModuleTests.cpp`. `ParametricEQModuleTest` — identity, 6-in/2-out channel layout, port labels, mod targets, logical-port roles, slot types and row labels, **all four bands start disabled**, enable round-trip and count, a disabled band with a big gain still contributing nothing, setters clamping to range, bypass dry pass-through / CV clearing / mute. `ParametricEQPointPlacement` — `findBandForNewPoint` picks the nearest free slot on a log axis, skips slots in use, returns -1 when full, and resolves out-of-range frequencies. `ParametricEQResponse` — `bandMagnitudeDb` anchor points (bell hits its gain at centre and 0 dB two decades out; cut is the exact mirror of boost; higher Q narrows the bell; a shelf sits at half its gain at the corner; zero-gain bands are flat everywhere; degenerate inputs return unity, not NaN) and `responseDb` skipping disabled bands plus adding the output trim. `ParametricEQCoefficients` — the RBJ digital biquad agrees with the analog prototype it came from within 0.6 dB, zero gain yields a literal pass-through biquad (`b == a`), and centres past Nyquist / a zero sample rate stay finite. `ParametricEQAudio` — real-audio level measurements (all-off is a straight wire; a configured-but-disabled band does not touch audio; enabling applies and disabling restores unity; ±12 dB bells move their band by ±12 dB; a narrow boost leaves distant tones alone; shelves only touch their own end; output gain scales everything; stereo channels come out identical) plus `MeasuredResponseTracksTheAnalyticCurve`, which locks the drawn curve to the measured DSP within 1 dB. `ParametricEQCV` — freq CV is exponential over the full range, gain CV maps onto ±24 dB and clamps, near-silent CV is gated to exactly the unmodulated value while CV above the gate gets through, and the shelves are provably *not* CV-modulated. `ParametricEQEdgeCases` — zero-length/zero-channel/mono buffers, processing without `prepareToPlay`, re-preparing at a new sample rate, band response independent of sample rate, state round-trip preserving enable flags, and every band exposing the full parameter set |
| AntiClickTest | 4 | ADSR minimum release, smooth parameter transitions |
| EdgeCaseTests | 21 | Zero-length buffers, extreme parameters, single-sample buffers, rapid parameter changes, large buffers |
| AudioRenderingTests | 26 | Snapshot-based tests comparing bit-perfect output against reference files; covers full chains (Osc->Filter->VCA), modulation accuracy, and External MIDI input |

### Integration Tests (~38 tests)

Test module interactions within the audio graph and cross-system integrations.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| IntegrationTest | 7 | Signal chain routing (Osc->Filter->VCA), LFO->Filter modulation, preset loading with graph structure validation |
| ModMatrixTest | 22 | Add/remove mod routings, amount scaling, channel mapping, modulation chains; Phase 4 adds: `RowHeightIs48` — `kRowHeight == 48`; `ZebraAlternates` — `isZebraRow` false for even, true for odd indices; `HoverStateUpdates` — `setHoveredRow`/`getHoveredRow` round-trip, default -1, redundant-set no-op, reset to -1; `ModMatrixComponentPaintSmokeTest` — paint with two routings, no crash; `HoverResetsAfterRowRemoval` — hover clears to -1 when a routing is removed (componentsChanged path) |
| OllamaProviderTest | 10 | AI LLM HTTP requests, streaming responses, model management, non-blocking discovery; `SendPromptWithNoModelFailsWithoutHittingNetwork` — fail-fast with no network call when `currentModel` is empty; `SendPromptIncludesSelectedModelInRequestBody` — captures the POST body and asserts `"model"` matches `setModel()` (regression lock for f7cba4a / empty-model 400s); `QueuedRequestDuringThreadShutdownStillCompletes` — a request enqueued as the worker winds down still gets a callback (request-loss race); `PendingRequestsAreFailedOnDestruction` — requests still queued at destruction are failed *before* `~OllamaProvider()` returns. Both use bounded `condition_variable` waits and fail on timeout rather than sleeping. **Never call `stopThread(0)` in a test** — it force-kills via `pthread_cancel`, which aborts under glibc |
| AIIntegrationServiceTest | 9 | Module suggestions, parameter recommendations, graph state mapping |

### Component Workflow Tests (~143 tests)

Test UI component interactions using in-process construction (no window, no display).

| Suite | Tests | What it covers |
|-------|-------|----------------|
| MainComponentTest | 27 | AI panel visibility toggle, mod matrix toggle, minimap toggle + button tooltip, default configuration, command manager registration (`CommandManagerHasCommands` is pinned to `ShortcutManager::getActionIds()`, so a bindable action with no command can't ship), redo shortcut, `CopyPasteDuplicateCommandsReachTheCanvas` + `PasteCommandIsInertUntilSomethingHasBeenCopied` (Paste carries `isDisabled` until the clipboard is filled, and `tryToInvoke` refuses an inactive command — that flag, not `perform()`, is what stops a stray Cmd+V); toolbar narrow/wide mode at 480/1600 px, library sidebar toggle + persistence, AI panel persistence, status bar bounds, canvas non-zero at minimum size, timer gating (5 Hz), patch name default + update on preset load, DrawableButton header buttons; `AiProviderGetsModelSelectedOnStartup` — regression lock (f7cba4a) that a model is selected on startup via `aiChatComponent.refreshModels()` called AFTER `setProvider()` |
| AIChatComponentTest | 3 | Initialization/resizing, send-message updates UI + history via mock provider; `RefreshModelsSelectsModelWhenProviderInstalledAfterConstruction` — reproduces MainComponent's member-init ordering (chat component constructed before a provider is installed), asserting `getCurrentModel()` stays empty until a post-construction `setProvider()` + `refreshModels()` |
| SelectionModelTests | 22 | Multi-select primitives (issue #156, `Source/UI/SelectionModel.h` — pure, no GUI). `SelectionModel` add/remove/toggle/setSelection/clear; `NodeID{0}` rejected as the graph's invalid sentinel; `getSelected()` ordered by uid regardless of insertion order (snippet node order must not depend on click order); `retainOnly` staleness pruning. `marqueeRectFrom` normalises a drag in all four directions. `hitTestMarquee` uses intersection not containment (a clipped edge selects), degenerate band selects nothing, invalid box ids skipped. `unionSelection` for the additive marquee |
| MultiSelectTests | 31 | GraphEditor-level multi-select (issue #156). Selection API (replace vs additive toggle, select-all, clear, prune after a node is removed behind the editor's back). Marquee: touches-to-select, replace vs add semantics, shrinking the band deselects, band over empty canvas deselects, update-without-begin is a no-op. Group drag: followers move by the initiator's delta, `finalizeSelectionDrag` preserves relative layout and snaps the *group* box to the grid, single selection does not engage group drag, `cancelSelectionDrag` leaves off-grid positions untouched, follower positions reach node properties. `deleteSelection` is ONE undo step for the whole group. Snippet drop through `itemDropped` with a `snippet:` payload; plain module drops still work; canvas Delete/Backspace/Escape keys return `false` when nothing is selected so they fall through |
| ClipboardTests | 32 | Copy / paste / duplicate for a multi-module selection (`Source/UI/ModuleClipboard.h` + the `GraphEditor` actions). **Clipboard state (pure):** empty by default, a payload with zero `nodes` still counts as empty (so Paste can't be offered for a selection of nothing but graph I/O), `kOffsetStep` is a multiple of the layout grid, each `nextPastePosition()` steps one offset further, a new copy restarts the cascade and `anchorAt` re-aims it. **Copy:** refused with nothing selected; an ineligible-only selection (Attenuverters) is refused *without* wiping what was already on the clipboard; the payload is a snapshot, so it still pastes after the originals are deleted; parameter values survive (a 0.5 Hz LFO rate) and so does non-parameter state (`…SoADuplicatedSamplerKeepsItsSample`). **Paste — wiring, the point of the feature:** `RewiresInternalConnectionsBetweenTheCopiesNotBackToTheOriginals` (copy↔copy wired, original↔original untouched, no wire crossing between them), `DropsConnectionsThatLeftTheCopiedSelection` (a half-selected wire is not recreated — the copy arrives with no wires at all), `DoesNotSpliceTheCopiesIntoTheSurroundingPatch` (merge-mode auto-connect stays off, so a pasted module never wires itself to Audio Output), `RebuildsModulationChainsBetweenTheCopies` (attenuverter count 1→2; the attenuverter never becomes a clipboard node). **Paste — placement:** no-op on an empty clipboard, copies left selected and originals not, relative layout preserved, lands one offset from the original rather than on top of it, repeated pastes cascade by exactly one step each, `pasteClipboardAt` snaps an off-grid point and re-anchors the cascade, ONE undo step for the whole group. **Duplicate:** no-op with nothing selected, offset copy with its own internal wiring, clipboard left untouched, works with no prior copy and does not implicitly fill the clipboard, repeats from the *new* selection so a chain walks across the canvas, ONE undo step |
| ModuleLibraryCollapseTests | 30 | Collapsible sidebar sections + Snippets section (issue #156). `DraggableModuleNamesExcludeSnippetsAndThePlaceholder` — `getDraggableModuleNames()` feeds callers that instantiate each name via the module factory, so it filters on `RowKind::Module`; snippet rows and the "No snippets yet" placeholder are visible but are not module types. `HitTestingAgreesWithTheRowLayout` — every row's centre maps back to its own entry (paint and hit-testing share `buildRows()`); rows never overlap; collapse hides a section's rows but keeps its header and shrinks `getTotalContentHeight()`; header click and top-strip click toggle; `toggleAllSections` folds from a partial state rather than unfolding; `onCollapseStateChanged` fires only for user-driven changes, never for the `setCollapsedSections` restore path; blank persisted state expands everything; Snippets section shows an empty hint with no snippets, becomes draggable rows when populated, and sits ahead of the module catalogue |
| ModuleLibraryCollapseAnimationTests | 11 | The 150 ms accordion fold. The tween is VBlank-driven and cannot tick headlessly, so these drive `setSectionProgress()` — the same value the animator writes each frame — and separately assert the snap paths: `CollapsingWhileHiddenSnapsInsteadOfHanging` and `RestoringPersistedStateSnaps` (a component that is not `isShowing()` must land on its final layout, or an off-screen sidebar would freeze mid-fold and every other headless test would see a half-open list). Endpoint parity — progress 1 reproduces the collapsed layout exactly, progress 0 truncates nothing. Band geometry — a half fold shows fewer rows, pulls the next header up by about half the section height, and shrinks `getTotalContentHeight()` monotonically across 0/0.25/0.5/0.75/1. `TheRowStraddlingTheBandEdgeIsTruncatedNotSquashed` — at most one row is clipped and none is emitted at zero height; `RowsFoldedPastTheBandStopHitTesting` — a folded-away row stops answering at its old position |
| ModuleLibraryScrollTests | 14 | Vertical scrolling for the library sidebar. The bar appears only once rows overflow the panel and hides again when they fit (including after `setAllSectionsCollapsed`, which also resets the offset to 0); `MaxScrollOffsetIsExactlyTheOverflow` — scrolled to the bottom the last row is fully reachable, no more and no less; the offset clamps on over/under-scroll and re-clamps when the panel grows; the bar sits below the pinned strip at the right edge. `MouseWheelScrollsTheRows` drives the real wheel path (component → `ScrollBar` → async listener → offset, so the helper pumps the message queue) and `MouseWheelDoesNothingWhenEverythingFits` guards the no-overflow case. `HitTestingFollowsTheScrollOffset` and `TheSameScreenYHitsDifferentRowsAtDifferentOffsets` guard the two coordinate spaces — `getEntryIndexAtComponentY()` applies the offset, `getEntryIndexAt()` stays content-space so existing `getRowCentreY()` callers are unaffected; `PinnedTopStripIsNeverARowHitWhileScrolled` — rows that scrolled under the COLLAPSE ALL chrome do not steal its clicks |
| GraphEditorTest | 19 | Module drag-and-drop, port connection via beginConnectionDrag/endConnectionDrag, deletion, mod matrix visibility, snap-on-drop (position is grid-multiple of 8), overlap resolution (second drop at same coord produces non-overlapping bounding boxes); `AudioFileDroppedOnCanvasCreatesAPreloadedSampler` — a real wav dropped on empty canvas yields a Sampler already holding it; `NonAudioFileDragIsRejectedByTheCanvas`; **Macro bank resize** — growing 4→16 knobs keeps the bank's top-left fixed, pushes the module below past `kCollisionGap` and persists its new y to node properties; shrinking 16→4 drops the routing on a hidden jack (and its attenuverter) while leaving a still-visible jack's routing intact |
| ModuleComponentTest | 21 | Initialization, resizing, parameter attachment to UI sliders; bypass/mute/delete are DrawableButtons with correct header bounds; delete button triggers requestDeleteModule; `SamplerHasLoadButtonWaveformAndKnownHeight` — the Sampler gets a `SampleWaveformComponent`, a "Load Sample..." button and a "(no sample)" label, at 280×657; `BodyContentClearsEveryPortLabel` — every visible body child starts below the lowest jack (regression guard for the overlap the old duplicated layout formula caused); `EstimatedModuleSizesMatchTheRealComponents` — builds every library-offered type and fails if `GraphEditor::estimateModuleSize` drifts from the real card, so the drag ghost cannot lie; `KnobsAreLaidOutThreePerRow`; `NonSamplerModulesGetNoSamplerChrome`; audio-file drop — `SamplerAcceptsAudioFileDropAndLoadsIt` (highlight on enter, cleared on drop, sample actually loaded), `SamplerIgnoresNonAudioFileDrag`, `NonSamplerModuleRefusesFileDragSoItFallsThroughToTheCanvas`; `WavetableCardBuildsDisplayAndLoadButton` — a Wavetable card owns a `WavetableDisplayComponent` and a "Load Wavetable..." button, both laid out inside the card, with 1 combo / 7 sliders / 2 toggles; `WavetableCardPaintsAndTicksWithoutCrashing`; **Macro bank layout** — height equals `macroBankHeight(count)` for 1/4/8/16 knobs, each output jack sits on its own knob row and hit-tests to that macro's index, no input or MIDI jacks, knobs clear the output-label gutter and stay inside the module, rows above the count are hidden |
| MidiKeyboardModuleTest | 4 | Note on/off, key press handling, velocity |
| VisualBufferTest | 3 | Scope visualization buffer management, read/write, ringbuffer behavior |
| ModuleBaseTest | 4 | Parameter getters, port labels, bypass functionality |
| ModuleBypassTest | 5 | Default state, toggle, signal passing when bypassed |
| FXBypassTest | 23 | Per-FX bypass dry pass-through, CV-channel clearing, mute silencing |
| VisualSignalFlowTests | 8 | AttenuverterModule peak/mod value tracking, VisualBuffer RMS computation, AudioEngine::getModulationDisplayInfo() population |
| SettingsWindowTest | 8 | Tab structure, tab persistence, audio device selector, AI settings persistence, resize safety, shortcuts reference |
| ShortcutManagerTest | 11 | Default bindings, reverse lookup, conflict detection, persistence round-trip, reset to defaults, display strings. `CopyPasteDuplicateUseThePlatformStandardKeys` pins Cmd+C/V/D. Two table-wide invariants that stop a new action from shipping broken: `EveryDefaultBindingIsUnique` (a duplicate binding silently shadows one of the two in `getActionForKeyPress`) and `EveryActionIdHasABindingACommandAndADescription` (an action with no `AppCommands` mapping binds a key that does nothing) |

**Poly connection creation coverage.** `GraphEditorTest` covers poly fan-out on drag (dragging a cable between two poly jacks creates all N per-voice connections) and poly-toggle rewire (toggling a module's `poly` parameter re-anchors its existing cables via `rewireForPolyChange`). `Tests/LogicalPortTests.cpp` adds pure, headless coverage of jack-target resolution — `ModuleBase::getJackTargets` and `GraphEditor::resolvePolyLink`'s pairing/scoring rules — independent of the audio graph.

#### `createComponentSnapshot` smoke-test pattern

Several new tests use `Component::createComponentSnapshot(bounds)` to verify that a component renders without crashing and produces non-empty pixels, without requiring a real display or window. Example: `StatusBarTests::RendersNonEmptyImage` and `ThemeTests::StyledWidgetSmokeTest.*`. The pattern is:

```cpp
comp.setSize(width, height);
auto img = comp.createComponentSnapshot({0, 0, width, height});
EXPECT_GT(img.getWidth(), 0);
```

#### AppProperties isolation pattern for persistence tests

Tests that read/write `ApplicationProperties` use an isolated temporary directory to avoid contaminating the shared settings file across test runs. The `MainComponent` exposes `getAppPropertiesForTest()` so the test fixture can target a temp dir:

```cpp
// In test SetUp():
auto tmpDir = juce::File::createTempFile("").getParentDirectory()
                  .getChildFile(juce::Uuid().toDashedString());
tmpDir.createDirectory();
// Write a value then verify MainComponent reads it back:
comp.getAppPropertiesForTest().getUserSettings()->setValue("librarySidebarVisible", "0");
// TearDown(): reset to defaults, then tmpDir.deleteRecursively()
```

### State Management Tests (~82 tests)

Test persistence, serialization, and state restoration.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| PresetManagerTest | 12 | Preset listing, load all presets, default preset validation, audio output connectivity, all 7 factory presets load with zero pairwise bounding-box overlaps at kCollisionGap=12; `AllPresetsPositionsOnGrid` (every baked x,y %8==0); estimateModuleSize mirror updated: Sequencer/PolySequencer/MidiKeyboard→560 (kDoubleWidth), Attenuverter excluded via `continue` |
| UndoRedoTest | 12 | Add/remove modules, connections, parameter changes, complex sequences, rapid operations, auto-arrange is a single undo step (one Cmd+Z restores all pre-arrange positions) |
| AIStateMapperTest | 26 | Graph JSON round-trip serialization, parameter validation, modulation serialization, merge mode, schema generation; `MergeAutoConnectsNewAudioNodesToOutputByDefault` / `MergeSkipsAutoConnectWhenTheCallerOptsOut` — locks the merge-mode convenience wiring (new audio node → Audio Output) that AI patches rely on and snippet insertion opts out of, so the two cannot drift into each other |
| SnippetManagerTests | 43 | Module snippets / grouping (issue #156, `Source/SnippetManager.{h,cpp}` — headless). **Extraction:** only selected modules; graph I/O nodes excluded even when selected; only connections with *both* endpoints inside the selection; positions normalised to the selection's top-left with relative layout preserved; `selectionOrigin` returns that same top-left, ignores ineligible/stale ids, and `AgreesWithTheOriginExtractionNormalisesAgainst` — the contract the clipboard leans on to place a paste relative to its source. **Extra state:** `includeExtraState` is off by default so a hand-editable `.agsnip` can never carry a `state` key into the trusted apply (`applyExtraStateToProcessor` reads it as a filename for Sampler), and `prepareForInsert` strips one even if a file has it; on, a Sampler's loaded file survives — that is the in-memory clipboard's opt-in; modulation stored as a `modulations` entry with the Attenuverter never becoming a snippet node; modulation leaving the selection dropped; stale/absent ids ignored. **Ids:** `nextFreeIdBase` above every existing uid; `prepareForInsert` renumbers + offsets, never mutates its input (the library inserts one loaded snippet repeatedly), clamps away from `NodeID{0}`. **Insert:** merges without disturbing a pre-existing module of the same type (the merge-collision regression), places the group at the drop point, `PreservesParameterValuesThatLookNormalised` (a 0.5 Hz LFO rate on a 0.01–20 range survives — the strict-validate / trusted-apply split), rebuilds modulation chains, two inserts give two independent copies, malformed JSON rejected without partially applying, dangling connections dropped, `DoesNotSpliceTheInsertedGroupIntoTheSurroundingPatch` + `DoesNotAttachInsertedModulesToAnExistingMidiSource` (merge-mode auto-connect opt-out — no wire may cross the snippet boundary on insert). **Persistence:** save/load/list/delete round-trip, insert from disk, name rewritten to the sanitised form, empty snippet and unusable name refused, corrupt files skipped by `listSnippets`, path separators stripped so a name cannot escape the directory, length capped |

### Output Level Tests (18 tests)

`Tests/OutputLevelTests.cpp` — the shared opt-in output-level stage (`ModuleBase::addOutputLevelParameter` / `prepareOutputLevel` / `applyOutputLevel`) and the modules that adopt it. Headless.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| OutputLevelHelper | 9 | Unity default is bit-exact pass-through; only the declared leading audio channels are scaled (CV channels untouched); level 0 silences; a 1.0→0.0 step ramps over 10 ms (max per-sample step < 0.01, reaches zero within the block) instead of clicking; **bypass passes dry audio at level 0**; mute clears at unity; a legacy state blob with no `outputLevel` property loads at unity (old presets sound identical); level survives a state round-trip. Uses an in-test `ModuleBase` subclass so the helper is exercised free of any module's DSP |
| OutputLevelModules | 9 | Across all 9 adopting modules (Delay, Reverb, Chorus, Phaser, Flanger, Distortion, Bitcrusher, Pitch Shifter, Filter): parameter present and defaulting to unity; **parameter added last in the list** (positional `getParameters()[n]` sites depend on it); level 0.5 halves the output sample-for-sample; level 0 silences; bypass still passes dry audio at level 0; mute clears at unity. Plus: Delay's level stays outside the feedback path (repeats survive a spell at level 0); Filter scales all 8 voices in poly mode; **`AttenuverterKeepsAmountAtParameterIndexOne`** pins the positional-parameter landmine shut |

### Module Adoption Tests (3 tests)

`Tests/ModuleAdoptionTests.cpp` — enforces the standing rule that **every module whose output carries audio has a level control**. Headless.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| ModuleAdoptionTests | 3 | A hand-maintained table classifies every module the factory can build into `SharedStage` (adopted `addOutputLevelParameter`), `OwnParameter` (has its own `level`/`gain`/`outputGain`/`inputGain`/`makeupGain`) or `NoLevelByDesign` (CV/gate/MIDI output, with a rationale string). `EveryAudioOutputModuleHasALevelControl` asserts each module matches its declared bucket — including that an `OwnParameter` module does **not** also adopt the shared stage (two level knobs on one panel). `EveryFactoryModuleIsClassified` is the tripwire: it harvests module names from `AIStateMapper::getModuleSchema()` and fails when a newly added module isn't classified, with a message naming the three buckets. `ClassificationTableHasNoStaleEntries` catches the reverse — a renamed/removed module leaving a dead row that silently stops enforcing anything |

### Layout Tests (~8 tests)

Pure/headless tests for the grid-layout and anti-overlap helpers in `Tests/LayoutUtilTests.cpp`.
No JUCE GUI components required.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| LayoutUtilTest | 16 | `snap` round-trips (negative-safe, midpoint), `intersectsAny` gap enforcement + selfId exclusion, `findFreeSlot` returns desired when clear, `findFreeSlot` resolves dense cluster (returned slot overlaps none), `computeAutoArrange` signal-depth layering (x strictly increases per depth, Audio Output in last column, no result-box overlaps); **width-bucket mapping** (Sequencer/PolySequencer/MidiKeyboard→Double, Attenuverter→Narrow, all others→Single); **bucket constants on grid** (kNarrowWidth/kSingleWidth/kDoubleWidth all %8==0, kDoubleWidth==2×kSingleWidth); **column stride** (kSingleWidth + kLayerGapX == 360); **Macro bank geometry** (`macroBankHeight` grows one `kMacroRowH` per macro, `macroRowCentreY` evenly spaced and always inside the bank); **`resolveOverlapsAfterResize`** (no-op when clear, pushes the neighbour below past the new bottom edge and on-grid, never moves the resized module, cascades through a stack until nothing overlaps, shrinking moves nobody back) |

### Theme Tests (~30 tests)

Tests for the theme system — `Tests/ThemeTests.cpp`. All headless.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| ThemeBuiltInsTest | 2 | Built-in theme registration (obsidian/neon/warm), WCAG AA contrast (≥4.5) for all built-ins |
| ThemeLoaderTest | 8 | JSON round-trip (exact colour/metric/typography/treatment equality), obsidian.gtheme.json vs makeObsidian(), required-key rejection, bad hex rejection, treatment float clamping, schema version rejection, style string round-trip |
| ThemeTest (fixture) | 4 | Persistence + restore via ApplicationProperties, broadcast on change / idempotency, unknown id rejection, user theme replace-by-id |
| ThemeLookAndFeelTest | 2 | All ColourId mappings from spec section 3, draw-helper smoke tests (fillThemedBackground / drawModulePanel / drawConnectionWire / drawModulationRing / drawRotarySlider into headless image) |
| ThemeLookAndFeelTest (extended) | 4 | `ApplyThemeSetsEveryColourId` extended to cover ListBox and TabbedButtonBar ColourIds; `RetintIconsCalledByApplyTheme` (getIcon non-null across 3 built-ins); `MetricsCodeOnlyFieldsHaveExpectedDefaults` (toolbarHeight==36, statusBarHeight==24, etc.); `MetricsCodeOnlyFieldsNotInJSON` (ThemeLoader output does not contain "toolbarHeight") |
| StyledWidgetSmokeTest | 9 | `drawComboBox` normal/pressed/disabled × 3 themes; `drawComboBoxTextWhenNothingSelected`; `drawPopupMenuItem` separator/highlighted/ticked/disabled/hasSubMenu; `getDefaultScrollbarWidth()==6`; `drawScrollbar` V/H × over/down; `drawScrollbarButton`; `drawTabbedButtonBarBackground` + `drawTabButton` active/inactive/hover with snapshot pixel check |

### Cable Colour Tests (22 tests)

Suite `Tests/CableColourTests.cpp` covering `Source/UI/CableColour.h` and the cable
enumeration / hit-testing added to `GraphEditor`. Layered: the pure resolver needs no GUI at
all, the canvas fixture builds a real two-module patch headlessly.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| CableColourCategoryTest | 3 | `categoryFor` totality over all 23 `ModuleType` values, groupings match the ModuleLibrary sections, persisted signal/category ids are unique and stable (`envelopes`, `modcv` spot-checked — renaming breaks saved user colours) |
| CableColourResolveTest | 7 | Each `CableSignal` maps to its theme token; `midiWire` differs from `audioWire` in every built-in theme; `BySourceCategory` uses the palette and ignores signal; the 8 category colours are mutually distinct per built-in theme; override precedence is mode-scoped; bypass alpha applies to the winning colour but NOT to `resolveCableBaseColour` (what swatches render); clearing an override restores the theme colour |
| CableColourPersistenceTest | 2 | Mode round-trip through `PropertiesFile`; override round-trip, untouched entries stay unset, reset removes the key entirely |
| CableColourThemeTest | 3 | `midiWire` + `cableCategory` survive a `themeToJson` → `parseTheme` round-trip; a pre-#157 theme with neither key still loads on defaults; a partial `cableCategory` object overrides only its named keys |
| CableGeometryTest | 2 | `buildCablePath` starts/ends exactly on the ports; `distanceToCable` ≈ 0 on the wire and large away from it |
| CableCanvasTest (fixture) | 5 | Enumeration reports the audio cable with the right signal + source category; hit-test hits a point sampled from the drawn curve and misses far away; tolerance is respected; `disconnectCable` removes the graph edge; `colourForCable` follows the active mode and overrides |

### Icon Library Tests (~11 tests)

Suite `Tests/IconLibraryTests.cpp` covering `Source/UI/Theme/IconLibrary.h/.cpp`.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| IconLibraryTest | 11 | `AllIconEnumValuesHaveEntry` — every Icon < kCount returns non-null getDrawable when assets present; `NullFallbackWhenAssetsAbsent` — no-assets path returns nullptr without crash; `ClonedDrawableIsIndependent` — two getDrawable calls return distinct raw pointers; `TintColourChangeApplied` — setTintColour(icon, c1) then setTintColour(icon, c2) → result contains c2 not c1; `RetintMultipleSwitchesStable` — 100× alternating setTintColour loop, final colour matches last set value; `SvgBinaryDataNamingConvention` — raw BinaryData symbol non-null (guards CMake renames); `TransportPlayIsScaffolding` — Icon::TransportPlay loads without crash; no DrawableButton wired this phase; Phase 4 adds: `WaveformIconsLoad` — all four WaveformSine/Saw/Square/Triangle return non-null; `WaveformIconsTintedToTextPrimary` — tint colour matches textPrimary after retintIcons(); `WaveformIconKTableCount` — static_assert count == 26 enforced at compile time; `WaveformBinaryDataSymbolsPresent` — waveformsine/saw/square/triangle_svg symbols non-null |

### Status Bar Tests (~9 tests)

New suite `Tests/StatusBarTests.cpp` covering `Source/UI/StatusBarComponent.h/.cpp` and AudioEngine additions.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| StatusBarTest | 9 | `ConstructsWithoutCrash`; `RendersNonEmptyImage` (createComponentSnapshot 400×24); `FormatCpu` — 0.0%, 75.6%, 100.0%; `FormatVoices` — 0→"0 voices", 1→"1 voice", 8→"8 voices"; `FormatPatch` — ""→"Untitled", named patch passes through; `GatedRepaintDoesNotFireOnUnchangedValues` — update() twice with same values triggers repaint only once; `AudioEngine_GetActiveVoiceInfo_ReturnsZeroWithoutPolyModules`; `AudioEngine_CountsPolyMidiVoices` — maxVoices==8 after adding PolyMidiModule; `MasterMute_ZeroesOutput` — setMasterMute(true) → output buffer all zeros post-processBlock |

### Frequency Response Tests (~50 tests)

`Tests/FrequencyResponseTests.cpp` covers `Source/UI/FrequencyResponseComponent.h`; `Tests/EQCurveTests.cpp` covers the axis maths both frequency-domain views share (`Source/UI/FrequencyGrid.h`), the Parametric EQ view and its interaction model (`Source/UI/EQCurveComponent.h`), and the pop-out editor (`Source/UI/EQWindow.h`).

| Suite | Tests | What it covers |
|-------|-------|----------------|
| FrequencyResponseTest | 13 | `PeakBinFindsMaximum`/`PeakBinFindsFirstMaxWhenTied`/`PeakBinHandlesEmpty`/`PeakBinSingleElement` — `findPeakBin` returns the max-magnitude bin (first index on ties, -1 for null/zero-length, 0 for single element); `FormatHzLabel_100Hz`/`_1kHz`/`_10kHz`/`_SubKiloHz`/`_FractionalKiloHz` — `formatHzLabel` yields "100Hz"/"1kHz"/"10kHz"/"440Hz"/"1.5…kHz"; `FreqMappingMonotonic`/`FreqMappingEndpoints` — `freqToXStatic` log map is monotonic and pins 20 Hz→x=0, 20 kHz→x=width; `DbMappingMonotonic` — `dbToYStatic` monotonic across +20/0/-20 dB; `PaintSmoke` — paints into a `juce::Image` with no crash, produces opaque pixels |
| FrequencyGridTest | 14 | `FreqToXSpansTheFullWidth`/`FreqToXIsLogarithmic`/`FreqToXIsMonotonic` — equal frequency *ratios* map to equal pixel distances; `XToFreqInvertsFreqToX`/`XToFreqHandlesZeroWidth`; `IndexToFreqSpansTheAxisAndIsMonotonic`/`IndexToFreqHandlesDegenerateCounts`; `DbToYPutsMaxAtTopAndMinAtBottom`/`DbToYIsMonotonicDownwards`/`YToDbInvertsDbToY`/`YToDbHandlesZeroHeight`; `FormatHzLabel`/`FindPeakBin`; `FilterViewStaticsDelegateToTheSharedGrid` — regression lock that `FrequencyResponseComponent`'s public statics still agree with `FrequencyGrid` after the mapping code was hoisted out of it |
| EQCurveTest | 8 | `StaticsUseASymmetricThirtyDbWindow` — ±30 dB window puts 0 dB at the vertical centre; `SpectrumIsOnByDefaultSoTheCurveHasABackdrop`; `FreqAtXAndGainAtYInvertTheDrawingTransform`; `GainAtYClampsToTheBandGainRange` — the ±30 dB view is wider than the ±24 dB parameter, so the top clamps; `PaintSmokeEmptyShowsTheHint`/`PaintSmokeWithActiveBands`/`PaintSmokeWithAllFourBandsAndSpectrum` — paint into a `juce::Image` with no crash and opaque pixels, including the FFT overlay driven by an explicit `timerCallback()`; `SilentInputDoesNotLeaveTheSpectrumRunning` — the silence gate that makes a default-on analyser free on an idle patch; `PaintAtDegenerateSizesDoesNotCrash` |
| EQCurveInteraction | 9 | The Cubase-style gestures, driven through the same methods the mouse handlers call. `AddPointEnablesTheBandAtTheClickedPosition`/`AddPointPicksTheSlotMatchingTheClickedFrequency`/`AddPointReturnsMinusOneOnceAllSlotsAreUsed`; `RemoveBandDisablesItAndClearsSelection`; `HitTestFindsEnabledHandlesAndIgnoresDisabledOnes` — including that a removed handle stops being hit-testable; `DragMovesTheBandInBothFrequencyAndGain` — with clamping past the edges; `ScrollAdjustsQMultiplicatively` — one notch doubles/halves Q and it saturates at the parameter bounds; `EveryEditIsBracketedByExactlyOneGesture` — balanced undo brackets, and a rejected add opens none; `DragIsNotBracketedPerStepSoOneDragIsOneUndoStep`; `ComponentPicksUpBandChangesMadeOnTheModule` — the card and pop-out window converge via the timer |
| EQWindowTest | 4 | `HostsACurveOverTheSameModule` — sizes itself, lays the curve out, and edits land on the shared module; `SpectrumToggleTracksTheCurve`; `ForwardsGestureCallbacksToTheCurve` — so pop-out edits are undoable; `PaintSmoke` |

### Scope Tests (~10 tests)

New suite `Tests/ScopeTests.cpp` covering `Source/UI/ScopeComponent.h`.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| ScopeTest | 10 | `NoSignalThreshold_Zero`/`_BoundaryInclusive`/`_JustAbove`/`_FullAmplitude` — `isNoSignal` is true for peak ≤ 0.02f (boundary inclusive), false above; `AmplitudeMapping_TopNearBoundsTop`/`_BottomNearBoundsBottom`/`_ZeroIsVerticalCentre`/`_Symmetry` — `amplitudeToY` maps +1→above centre, -1→below centre, 0→exact vertical centre, symmetric about centre; `PaintSmokeNoSignal`/`PaintSmokeWithSignal` — paint the silent (No-Signal empty-state) and signal states into a `juce::Image` with no crash |

### Wavetable Oscillator Tests (30 tests)

New suite `Tests/WavetableOscillatorModuleTests.cpp` covering `Source/Modules/WavetableOscillatorModule.h`.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| WavetableOscillatorModuleTest | 27 | `FactoryInitialisation` — type/name/11 params/13 channels/6 visible in-jacks; `DeclaresEnoughOutputsForEveryCVInput` — guards the buffer-aliasing invariant (highest poly CV channel < `getTotalNumOutputChannels()`); `PortLabelsAndModulationTargets`, `LogicalPortMappingMonoAndPoly` — jack labels, mono ch0-5 mapping, poly pitch fan (`isPolyGroupHead`/`polyVoiceSpan == 8`), shared CV ch8-12, `isAutoPromotableModTarget` false in poly; `ZeroChannelsDoesNotCrash`; `ProducesAudioOnChannelZero` — audio on ch0, silence on ch1-12; `DefaultTablePositionZeroIsASine` — >95% of energy at the fundamental; `ScanningPositionChangesTheSpectrum` — position 1.0 adds ≥10× third-harmonic energy; `EveryBuiltInTableProducesAudio` — all 6 built-ins sound and report 32 frames; `LoadedFileChoiceFallsBackWhenNothingIsLoaded`; `OutputStaysBounded` — 8-voice unison + detune stays under ±2.0; `LowSampleRateWithExtremeTuningStaysFinite` — 8 kHz sample rate with octave +4 / coarse +12 (>1 cycle per sample) stays finite and in range, guarding the phase wrap; **`HighNotesDoNotAlias`** — square at MIDI 108, worst magnitude in 200 Hz–3.5 kHz is <5% of the fundamental (the mip-selection guard); `PositionCVScansTheTableInMonoMode`, `LevelCVAttenuatesInMonoMode`; `PolyModeRendersOneVoicePerPitchCVChannel` — 3 pitch CVs sound, voices 3-7 silent, CV ch8-12 do not leak; `PolyModePositionCVScansAllVoices`; `OctaveParameterTransposesInPolyMode` — 440 Hz CV + octave 1 sounds at 880 Hz; `LoadWavetableFileSplitsFrames`, `LoadedFramesKeepTheirDistinctHarmonics` — frame 0 is the fundamental, last frame the 4th harmonic; `LoadWavetableFileRejectsMissingAndInvalidFiles` — returns false, keeps playing the built-in; `LoadWavetableFileCapsFrameCount` — a >64-frame file decimates to `kMaxFrames`; `ReloadingWhileRenderingStaysStable` — 6 alternating loads interleaved with `processBlock` (exercises the pending/retired handoff); `StateRoundTripRestoresParametersAndWavetable`, `StateRoundTripSurvivesAMissingWavetableFile`; **`TrustedGraphJSONRestoresTheWavetable`** — round-trips through `graphToJSON`/`applyJSONToGraph`, the path presets and undo actually use (a wavetable path living only in `getStateInformation` is dropped there); `UntrustedPatchCannotNameAWavetableFileToOpen` — model-authored JSON must not reach `setExtraState` |
| WavetableMipGeometry | 1 | `LimitsDecreaseMonotonicallyToTheFundamental` — mip 0 holds 1023 harmonics, the coarsest holds 1, limits strictly decrease, and every mip's limit is within its own Nyquist with length ≥ 64 |
| MuteAndBypass/WavetableMuteBypassTest | 2 | `OutputIsSilentWhenMutedOrBypassed` — parametrized over mute and bypass; a pure source module clears on both (the documented `OscillatorModule` exception) |

### Wavetable Display Tests (8 tests)

New suite `Tests/WavetableDisplayTests.cpp` covering `Source/UI/WavetableDisplayComponent.h`.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| WavetableDisplayTest | 8 | `QuantisePositionEndpointsAndClamping`/`QuantisePositionIsMonotonicAndCollapsesTinyChanges` — `quantisePosition` pins 0→0 and 1→`steps`, clamps out-of-range input, is monotonic, and maps sub-bucket jitter to the same bucket (this is the repaint gate); `RepeatedTimerTicksOnAnUnchangedModuleAreIdempotent` — five ticks leave the trace bit-identical, a real position change is still picked up; `DisplayWaveformTracksScanPosition` — position 0 and 1 traces differ by >0.2 and both stay bounded; `DisplayWaveformHandlesTinyPointCounts` — 0 and 1 requested points still yield ≥2 samples; `PaintSmokeBuiltInTable`/`PaintSmokeAtEveryScanPosition`/`PaintSmokeAtDegenerateSizes` — paints into a `juce::Image` with no crash at 11 scan positions and at 0×0/1×1/4×80 bounds |

### Minimap Tests (25 tests)

New suite `Tests/MinimapComponentTests.cpp` covering `Source/UI/MinimapComponent.h/.cpp` (issue #159). See [`layout.md` §15](layout.md#15-minimap-overlay-issue-159) for the feature.

| Suite | Tests | What it covers |
|-------|-------|----------------|
| MinimapComponentTest | 19 | `computeWorldBounds` — empty model falls back to a `kMinWorldSpan` square at the origin, nodes-only/viewport-only/both are contained, a single small node is clamped out to the min span while staying centred on it; `computeWorldToMap` — preserves aspect ratio in a non-square map area, maps inside and centres in the map area, zero-width world / zero-height map area stay NaN/Inf-free; `mapToWorld` round-trips several points through the forward transform; `setModel` reflects a differing model and no-ops on an equal one (observed via `getModel()`); `setViewport` updates only the viewport, leaving nodes/cables untouched; construction at a realistic size and a non-empty tooltip; paints a non-empty image after `setModel`; `mouseDown`/`mouseDrag` fire `onNavigate` with the point `mapToWorld` itself predicts; mouse and wheel handlers are safe no-ops with `onNavigate`/`onZoom` unset |
| MinimapModelTest | 6 | `MinimapModel::operator==`/`!=` — identical models are equal; differing viewport, node count, a moved node, a node differing only in `selected`, or a cable differing only in colour all make models unequal |

### E2E Workflow Tests (24 tests)

Full application workflow tests in `Tests/E2EWorkflowTests.cpp`. Each test constructs a complete `MainComponent` with a mock AI provider and exercises real UI interaction code paths.

| Area | Tests | What it covers |
|------|-------|----------------|
| App Initialization | 3 | Default patch has nodes/connections, panel toggle visibility, fresh undo state |
| Preset Management | 3 | Load preset updates graph, load all 7 presets without crash, load-modify-undo |
| Module Management | 4 | Drop module via `itemDropped()`, drop all 17 module types, delete module, replace module type |
| Connections | 4 | Connect ports via `beginConnectionDrag()`/`endConnectionDrag()` with `localPointToGlobal()` coordinate conversion, disconnect, MIDI connections, mod routing creates attenuverter |
| Mod Matrix | 4 | Add empty routing, configure source/dest, adjust CV amount, delete routing |
| Undo/Redo | 4 | Undo add-module, complex sequences, preset-load-then-modify, rapid 5-module sequence |
| Combined Workflows | 1 | Full preset-modify-connect-undo-redo workflow |
| Layout / Auto-Arrange | 1 | Load preset, call autoArrange(); all module comps non-overlapping AND connection/node counts unchanged |

#### Key patterns

- **Coordinate conversion**: Components are nested inside MainComponent, so connection tests use `localPointToGlobal()` to convert port positions to screen coordinates for `endConnectionDrag()`.
- **Relative counts**: The default patch creates ~14 nodes, so all tests use `initialCount + N` rather than absolute values.
- **Module lookup**: `findNewModule(name, initialNodeIDs)` finds only modules added after a snapshot, avoiding false matches with default patch modules.
- **Preset loading**: Must call `editor().detachAllModuleComponents()` before `PresetManager::loadPreset()` to avoid use-after-free.

### AI Patch Validation Tests (~19 tests)

`Tests/AIPatchValidationTests.cpp` is table-driven: one deliberately malformed patch per
`PatchValidationError` value, each asserting the **exact** enumerator rather than merely "was
rejected". `EveryErrorValueIsCovered` walks the enum and fails if a newly added value has no case,
so the table cannot silently fall behind the enum.

The same file pins the `getPatchSchema()` contract — the module-type enum matches the factory,
choice parameters are enumerated, `additionalProperties` stays open for numeric parameters, and no
reference data is offered as an output field.

`Tests/AIPatchRetryTests.cpp` covers `applyPatchWithRetry` with a scripted provider double that
answers synchronously (so no message loop is needed): that the validation message reaches the
model, that retries stop at `kMaxPatchRetries`, and that the mode repair fires only for a
rejected, mode-less patch and never in the destructive merge-to-replace direction.

## Measurement Harness (not a test)

`Tools/AIPatchHarness` measures how often real model output passes `validatePatch`, replaying a
fixed prompt set through the production path and tallying rejections by error value. It needs a
live Ollama, so it is opt-in and never built by CI:

```bash
cmake -S . -B build -DENABLE_AI_HARNESS=ON
cmake --build build --target AIPatchHarness
./build/Tools/AIPatchHarness/AIPatchHarness --model <tag> --runs 2
```

See `Tools/AIPatchHarness/README.md` — in particular that `format` enforcement is backend-dependent
and that `OllamaProvider` times out at 240 s (`kChatRequestTimeoutMs`), which shows up as a
provider error rather than a rejection.

`Tools/AIEvalHarness` scores a different thing: of the patches that pass validation and apply, are
they actually usable — has an output, that output is reachable from a real sound source, every
parameter in range? It replays 40 golden prompts and runs `Source/AI/PatchEval.h`'s checks against
the resulting graph. Same opt-in flag:

```bash
cmake -S . -B build -DENABLE_AI_HARNESS=ON
cmake --build build --target AIEvalHarness
./build/Tools/AIEvalHarness/AIEvalHarness --model <tag> --runs 2
```

The structural checks themselves (`evaluatePatch()`) have no model dependency and are covered by
`Tests/PatchEvalTests.cpp` in the regular suite — only the golden-prompt replay needs Ollama.

Note that `evaluatePatch` is not only a scoring function: `AIIntegrationService::applyPatch` gates on
`sourceReachesOutput` and surfaces `detail` to the user via `getLastPatchError()`, so those strings are
a contract two `AIIntegrationServiceTest` cases assert verbatim. `SamplerAloneDoesNotCountAsAReachableSource`
/ `SamplerAlongsideAnOscillatorStillPasses` cover the one module that is a sound source in the library
but deliberately not one here (a Sampler is silent until a file is loaded, and a model-authored patch
cannot load one).

## Adding Tests for New Modules

When adding a new audio module:

1. **Unit tests** in `Tests/<ModuleName>Tests.cpp` — test DSP output, parameter handling, edge cases
2. **E2E coverage** — add the module's name string to the `moduleTypes` array in `E2EWorkflowTest.DropAllModuleTypes_NoCrash`
3. **Add to `Tests/CMakeLists.txt`**

## Snapshot Testing

The `AudioRenderingTests` suite compares rendered audio against "golden" reference files stored in `Tests/reference/`.

- **To run**: `./build/Tests/Tests --gtest_filter="AudioRenderingTest.Snapshot*"`
- **To update references**: If you intentionally change DSP logic (e.g., a better filter algorithm) and want to update the baseline:
  ```bash
  bash scripts/update-reference.sh
  ```
- **To listen**: Use `scripts/play-reference.sh <filename>` (requires `ffplay` or `aplay`).

## Build

```bash
cmake -S . -B build
cmake --build build
```

`ENABLE_TESTS` defaults `OFF` — pass `-DENABLE_TESTS=ON` to generate the `Tests` target. `ENABLE_COVERAGE` is a separate opt-in flag used by the Ubuntu CI job and `scripts/coverage.sh`.

## Git Hooks

Install once per clone (hooks are **not** auto-installed):

```bash
bash scripts/install-hooks.sh
```

Two hooks are registered:

- **pre-commit** (`scripts/pre-commit-lint.sh`): runs `clang-format --dry-run --Werror` on staged `Source/` and `Tests/` C/C++ files. Fast; mirrors the CI Lint job. Also warns if the local `clang-format` version differs from the pin in `.clang-format-version`.
- **pre-push** (`scripts/pre-push-release-test.sh`): runs clang-format lint on all C/C++ sources, then a Release build + full test suite. The first push configures the `build-release/` directory; subsequent pushes are fast incremental rebuilds. This catches UB and segfaults that Debug mode hides (zero-initialized memory masks use-after-free). Also warns if the local `clang-format` version differs from the pin in `.clang-format-version`.

Bypass a single invocation with `--no-verify`:

```bash
git commit --no-verify
git push --no-verify
```

Run manually at any time:

```bash
bash scripts/pre-commit-lint.sh        # lint staged files
bash scripts/pre-push-release-test.sh  # lint + Release build + tests
```

**clang-format version note:** clang-format is pinned via the PyPI `clang-format` wheel to the version recorded in `.clang-format-version`. CI installs that exact version with `pip install "clang-format==$(cat .clang-format-version)"` (after `actions/setup-python`), so CI and the local hooks run the identical binary — eliminating "hook passes locally but CI fails" drift. Install or update locally with the same command:

```bash
pip install "clang-format==$(cat .clang-format-version)"
```

## CI Pipeline

CI runs via `.github/workflows/ci.yml` on pull requests to `main` **and on pushes to `main`**, path-filtered to changes under `Source/**`, `Tests/**`, `Tools/**`, `CMakeLists.txt`, `Tests/CMakeLists.txt`, `cmake/**`, `scripts/**`, or either workflow file (`ci.yml` / `build-artifacts.yml`).

The push-to-`main` trigger exists **to seed the build caches** — see [Build caching](#build-caching). Removing it silently doubles every PR's build time, so it is load-bearing, not redundant with the artifact workflow.

**Push runs build only.** Linting, test execution and the coverage check are gated to `pull_request`: they already ran on the PR against this same tree, so repeating them on the merge result costs runner minutes without adding signal — the position `build-artifacts.yml` has always taken for the release build. This does not weaken the cache, because `cmake --build build` still compiles the `Tests` target; only *running* the binary is skipped, so every test translation unit still lands in ccache. It trims a push run from ~9.4 to ~6 runner-minutes; the macOS job was the worst offender, an 8-second cached build carrying 118 seconds of tests.

What this gives up: a post-merge run no longer catches two PRs that are each green alone but conflict once both land. The exposure is small — the next PR's CI runs against the merged result, and the release build still fails if the merge does not compile.

> **Do not rename the `Lint`, `Build, Test, and Coverage`, `Build and Test (macOS)` or `Build and Test (Windows)` jobs.** Those four strings are configured as required status checks in `main`'s branch protection; renaming one leaves a required check permanently pending and blocks every merge. Skipping a job on `push` is safe (protection only gates pull requests) — renaming it is not.

A `concurrency` group cancels superseded runs on the same PR (main is never cancelled, since those runs seed the cache).

There are **five jobs**:

| Job | Runner | Config | Notes |
|-----|--------|--------|-------|
| **Lint** | `ubuntu-latest` | — | Installs the pinned `clang-format` PyPI wheel (version from `.clang-format-version`) via `pip install "clang-format==$(cat .clang-format-version)"` after `actions/setup-python`; runs `--dry-run --Werror` over `Source/` and `Tests/`. Fast (~30 s) — gives formatting feedback without waiting for a full build. |
| **Build, Test, and Coverage** | `ubuntu-latest` | Debug + clang + `ENABLE_COVERAGE=ON` | Runs tests, then `bash scripts/coverage.sh --report-only` (skips re-build; only merges profdata and checks the 85% line-coverage threshold). |
| **Build and Test (ASAN)** | `ubuntu-latest` | `RelWithDebInfo` + `-fsanitize=address` | **Label-gated** — only runs when the PR carries the `run-asan` label. `ASAN_OPTIONS=detect_leaks=0`. |
| **Build and Test (macOS)** | `macos-latest` | Release | Catches UB/segfaults and cross-platform issues. |
| **Build and Test (Windows)** | `windows-latest` | Release | Catches UB/segfaults and cross-platform issues. |

### Optimizations

- **`JUCE_WEB_BROWSER=0`**: Drops unused WebBrowserComponent and removes WebKit/libsoup deps on Linux.
- **Separate lint job**: Instant formatting feedback without waiting for a full build.
- **apt package caching**: `awalsh128/cache-apt-pkgs-action` caches Ubuntu packages across runs.
- **`coverage.sh --report-only`**: In CI, skips redundant configure/build/test steps and only merges profdata + generates the report.

### Build caching

Two caches carry work between runs. Both are easy to break in ways that look exactly like a healthy build, just slower — which is why `scripts/ci-cache-check.sh` gates them (see below).

- **ccache** — compiler cache. `CMAKE_C/CXX_COMPILER_LAUNCHER=ccache`, 2 GB max size, `CCACHE_DIR` pinned explicitly to `${{ github.workspace }}/.ccache` on every platform. Keyed `<os>-ccache-<ref>-<sha>`; the restore-keys fall back this ref → `main` → anything. Also sets `compiler_check=content` and `sloppiness=time_macros,include_file_mtime,include_file_ctime`, because `actions/checkout` rewrites every file's mtime each run and the compiler binary's mtime changes whenever GitHub rebuilds the runner image — under ccache's mtime-based defaults both produce false misses.
- **FetchContent** — `build/_deps` (JUCE + GoogleTest sources), keyed on `hashFiles('cmake/DependencyVersions.cmake')`.

#### Three rules, each learned from an outage

1. **`ci.yml` must keep its `push: main` trigger.** GitHub scopes a cache to the ref that wrote it; a PR run can read its own ref and the **base branch**, nothing else. From the introduction of ccache until Aug 2026 this workflow ran on `pull_request` alone, so it never wrote a cache into `main`'s scope and **no PR ever restored one** — every run compiled the entire tree cold for months. The logs said `Cache not found for input keys: …` and CI passed regardless.
2. **Key `build/_deps` on the dependency pins alone**, never on `CMakeLists.txt`. `build/_deps` holds nothing but fetched sources, so adding a module must not invalidate it. The old `hashFiles('CMakeLists.txt', 'Tests/CMakeLists.txt')` key minted a fresh ~350 MB entry per platform per workflow on every module PR; with 10 `CMakeLists.txt` edits in the first nine days of Aug 2026 that pushed the repo past **GitHub's 10 GB per-repo cache limit**, which LRU-evicted the ccache entries too. All pins therefore live in `cmake/DependencyVersions.cmake` and the `FetchContent_Declare()` calls read them from there, so key and pin cannot drift.
3. **`CCACHE_DIR` must be set explicitly.** ccache's default directory varies by platform *and* version. The Linux job cached `~/.ccache` while ccache 4.x on `ubuntu-24.04` writes to `~/.cache/ccache`, so the Linux ccache was never even saved — no `Linux-ccache-*` entry ever existed. In `build-artifacts.yml` the path is a **matrix value**, since one job spans three runners and a job-level override is unavailable.
4. **Cache steps use `actions/cache/restore` + `actions/cache/save`, not the combined `actions/cache`.** The combined action declares exactly one output, `cache-hit`, true only on an *exact* primary-key match — and the ccache primary key embeds `github.sha`, so it never matches exactly even when a restore-key fallback works perfectly. Only `actions/cache/restore` exposes **`cache-matched-key`**, the one value that reports a fallback hit. Reading it off the combined action yields an empty string forever: run `31301691346` restored every cache and compiled at a **100% ccache hit rate** while the check reported `MISS` on all three platforms; enforcement would have failed every build. The split also lets the save step run `if: always()`, so a successful compile is not discarded because a later test failed.

#### The cache health check

`scripts/ci-cache-check.sh` runs after the build on Linux, macOS, and Windows. It reads each cache step's `cache-matched-key` output plus `ccache --show-stats`, writes a summary table to the job summary, and:

- **fails** when a cache that should have restored did not;
- **warns** when the ccache hit rate is under `CACHE_MIN_HIT_RATE` (default 25%), which drops legitimately whenever a PR touches a widely-included header.

`CACHE_WARM_EXPECTED` decides which runs are held to that standard. It is true **only for pull requests from a branch in this repository**. Two cases are legitimately cold and are reported as a notice instead:

- the **push-to-`main`** run, which is what seeds the cache in the first place;
- a **pull request from a fork** — GitHub gives forks an isolated cache scope with no read access to the base repository's entries, so a miss is guaranteed and is nothing the contributor can fix. Without this exemption, enforcement would fail every outside contribution on its first run.

Enforcement is controlled by the repository variable **`CI_CACHE_CHECK_ENFORCE`** (Settings → Secrets and variables → Actions → Variables), **currently `true`** — a cold cache fails the build. It is a variable rather than a hard-coded value so enforcement can be switched off without a code change if a runner-image or `actions/cache` change ever starts producing false alarms.

`build-artifacts.yml` runs the same check but pins it to report-only: losing a release over a cache miss would be a worse outcome than a slow release.

The check also **fails safe**: an empty `cache-matched-key` contradicted by a high ccache hit rate (≥ `CACHE_SELFCHECK_HIT_RATE`, default 50%) is reported as *a misconfigured check*, not a cold cache, and does not fail the build — a cold build cannot hit 100%, since its only hits are files compiled into two targets in the same run (~15%). A genuinely cold cache still fails. The guard keys on the **ccache** contradiction alone: a high hit rate proves the ccache restored and both keys share the same plumbing, but it proves nothing about `build/_deps`, so a deps-only miss with a populated ccache key is still a real failure.

The script has its own tests (`scripts/tests/ci-cache-check.test.sh`, 15 cases, run by the Lint job): if the thing that detects a broken cache breaks silently, we are back to shipping cold builds unnoticed. Those tests scrub every input variable before each case — `ci.yml` exports `CACHE_CHECK_ENFORCE` and `CACHE_WARM_EXPECTED` workflow-wide, the Lint job inherits them, and a non-hermetic harness silently inherited CI's values and passed cases it should have failed.

To inspect cache state directly:

```bash
gh api "repos/:owner/:repo/actions/caches?per_page=100" \
  -q '.actions_caches[] | "\(.size_in_bytes/1048576|floor)MB\t\(.ref)\t\(.key)"'
# Total size — evictions start once this approaches GitHub's 10 GB limit:
gh api "repos/:owner/:repo/actions/caches?per_page=100" -q '[.actions_caches[].size_in_bytes]|add/1073741824'
```

### What didn't work

- **Unity builds** (`CMAKE_UNITY_BUILD`): Incompatible with JUCE — Obj-C++ `.mm` files cannot be merged into C++ unity translation units.
- **Precompiled headers**: JUCE module `.cpp` files have guards against being pre-included; on macOS, `.mm` files also require Obj-C++ mode which conflicts with a C++ PCH.

### Post-merge artifact builds

After a merge to `main`, `.github/workflows/build-artifacts.yml` triggers automatically. It builds and packages the app on Ubuntu, macOS, and Windows (no tests — CI already ran them on the PR) using **Ninja on all three platforms** (Windows via the MSVC dev environment), so the build path matches the PR CI exactly; it then bumps the version tag and creates a GitHub release with all three platform artifacts.

It uses ccache as well, under a distinct `<os>-release-ccache-` key — the Release/no-tests configuration produces different objects from `ci.yml`'s, so the two must not share a cache scope. Until Aug 2026 this workflow had no compiler cache at all and recompiled the whole tree from cold on every merge, even though PR CI had just built the identical commit. The tag-and-release step runs **only on `push` to `main`** — a manual `workflow_dispatch` run is a build-only dry-run, useful for validating the matrix (including the Windows build) before merging. Docs-only / non-code merges are skipped via a `paths-ignore` filter: pushes that touch only `**/*.md`, `docs/**`, `LICENSE`, `.gitignore`, `.clang-format`, `.clang-format-version`, `.claude/**`, or `mockups/**` produce no build, no version bump, and no release; mixed code+docs merges still release as normal.
