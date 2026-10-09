# Adding Tracks

The `"+ Track"` button at the top of the timeline panel's header column, and every flow it starts.
The header rows it creates are [tracks](tracks.md); the channel chains it builds are
[`docs/mixer/mixer.md`](../mixer/mixer.md#building-a-channel).

## The picker

`"+ Track"` is the first keyboard stop of the header column: Down from the bottom-panel tab strip (Timeline selected) or from the Timeline region root lands on it
and Down again on the first track; it is also the stop below the last track (the arrow-key table is in
[shortcuts](../control/shortcuts.md#timeline)). Its tooltip names the live **Add Track...** shortcut (Ctrl+T on
macOS, Ctrl+Alt+T elsewhere, rebindable). Pressing it, or the shortcut, opens a **searchable picker** in a call-out
under the button: the same shared list as a module card's "Replace with..." (`synth::ui::ModMatrixPicker`). The search
field has focus, so typing filters at once (all words, any order); Up/Down move, Return picks the first match until
moved, Escape closes. The rows sit under these headers:

- **Tracks**: MIDI Track / Audio Track
- **Instrument Tracks**: Oscillator / Wavetable / Sampler and the poly variants
- **Plugins**: the scanned instrument plugins (a greyed "Scanning for plugins..." or "No instrument plugins found" row when there are none)
- one header per saved track preset kind (**Audio Track from Preset**, **Instrument Track from Preset**, **Bus from Preset**)
- **More**: Insert Track Preset from File..., Add Marker, Create Channels (greyed, with the reason, when every track already has a channel)

`TimelinePanelComponent::buildAddTrackMenu()` is still the single source of the entries and of the build-time
snapshots below. `openAddTrackMenu()` builds that `juce::PopupMenu`, appends Create Channels, and
`synth::ui::flattenAddTrackMenu` (`AddTrackPicker.h`) turns it into picker rows: each row's id IS the menu item's id,
so a pick calls `finishAddTrackMenu(id, ...)` -> `applyAddTrackMenuChoice(id)` unchanged. The submenu title becomes the
header, and the preset kinds and plugins carry extra search words ("plugin instrument vst au", "preset", "marker flag").

Menu ids are `TimelinePanelComponent::kAddMidiTrackMenuId` / `kAddAudioTrackMenuId` /
`kAddInstrumentOscillatorMenuId` / `kAddInstrumentWavetableMenuId` / `kAddInstrumentSamplerMenuId`
/ `kAddMarkerMenuId`. The MIDI and Audio entries land on `TrackHeaderHost` (`addMidiTrack()` /
`addAudioTrack()`); each Instrument entry calls `addInstrumentTrack(instrumentModuleType)`
with its module type string.

**Focus.** Opened from the keyboard (the button was focused, or the shortcut ran), the picker hands focus back to
`"+ Track"` when it closes: `finishAddTrackMenu` does it after a pick, and the picker's `onClosed` (run from its
destructor) does it after Escape or a click away, once only. The shortcut itself is
`TimelinePanelComponent::openAddTrackMenuFromShortcut()`: it focuses the button, then opens the picker as a keyboard open;
`MainComponent` runs it after showing the Timeline.

`TimelinePanelComponent::applyAddTrackMenuChoice(id)` is the headless seam for all of them. A picker never runs
headless in the test binary, so `synth::ui::test_hooks::addTrackPickerHookForTest()` receives the built picker instead of
a call-out (`TimelineAddTrackPickerTests.cpp`). `MainComponent::simulateAddMidiTrackClick()` / `simulateAddAudioTrackClick()` /
`simulateAddInstrumentTrackClick(menuId)` call straight into the seam. `openAddTrackMenu()` is the
`protected virtual` that opens the real picker; see
[ruler](ruler.md#opening-a-menu-is-a-protected-virtual) for why that seam exists.

## Menu options resolve against a build-time snapshot

Every real plugin entry's id is `kAddInstrumentPluginMenuIdBase + index`, where `index` indexes
`TimelinePanelComponent::collectInstrumentPluginMenuOptions()`'s result AT BUILD TIME — captured
into the member `instrumentPluginMenuSnapshot_` the moment the menu is built by
`buildAddTrackMenu()`. `applyAddTrackMenuChoice` resolves strictly against that snapshot, never by
re-collecting.

**Why, and why this differs from the timeline's automation lane choices.** The known-plugin list
backing this menu is mutated by `PluginScanService::runScan` on a BACKGROUND thread and re-sorted
by name in `MainComponent::getInstrumentPluginOptions()`. A scan completing between the menu
opening and the click landing can otherwise change what index N means, silently resolving the click
against a plugin the menu never actually showed there — the live symptom is a menu showing
"Massive", clicking it, and getting a Sampler track instead. The automation lane choices
(`collectAutomationLaneOptions` / `applyAutomationLaneMenuChoice`) deliberately re-runs its
collector at click time, because an automation lane is document data mutated only on the message
thread, so that is safe.

The track-preset lists follow the same build-time-snapshot rule
(`audioTrackPresetMenuSnapshot_` / `instrumentTrackPresetMenuSnapshot_`), since a preset can be
saved or deleted between the menu opening and the click landing.

## Left/right jacks preference

Modules a new track builds follow **Preferences -> "Split Left/Right jacks on new modules"** and its
per-module overrides, the same as a library drop: the instrument node and the channel's insert FX
(Gate, Parametric EQ, Compressor). Core's chain builders cannot see `GraphEditor`, so
`MainComponent::newModuleHook()` is handed in as `DefaultChannelLayout::onNewModule` and runs on each
insert right after creation, before it enters the graph. The infrastructure the chain forces a shape on
(Channel Strip, Master, Track In/Track Audio, Voice Mixer, ADSR/VCA/Poly MIDI) ignores the preference.
Track presets are different: they keep the jacks they were saved with.

## Track presets

Below the Instrument submenu, `"+ Track"` lists every saved preset for each track kind — own
**Audio**, **Instrument** and **Bus** submenus, one item per preset via
`synth::TrackPresetManager::listTrackPresets`, ids `kAddTrackPresetAudioMenuIdBase` /
`kAddTrackPresetInstrumentMenuIdBase` / `kAddTrackPresetBusMenuIdBase` — then a final **"Insert
Track Preset from File..."** entry that loads one saved anywhere on disk.

**FRO297 (docs/mixer/track-presets.md#a-third-kind-bus): the Bus submenu is the odd one out.**
Choosing one of its entries (`TrackHeaderHost::addBusFromPreset`) creates NO timeline track at
all — a bus has none — just the bus chain, which the mixer shows as a BUS column exactly like a
freshly built one. It follows the same build-time-snapshot rule as the other two
(`busTrackPresetMenuSnapshot_`). **"Insert Track Preset from File..."** dispatches the same way: a
file whose own `"trackPresetKind"` reads `"bus"` inserts through the no-timeline-track path instead
of creating a track.

This is a **separate** path from the plain MIDI/Audio/Instrument entries above: those consult only
each type's *default* preset (Preferences → Mixer), while this submenu can insert ANY saved preset
regardless of which one is currently the default. See [`docs/mixer/track-presets.md`](../mixer/track-presets.md#what-a-track-preset-is).

## The Plugin sub-submenu

The Instrument submenu's **Plugin** sub-submenu lists the scanned INSTRUMENT hosted plugins
(`TrackHeaderHost::getInstrumentPluginOptions()`), filtered on
`juce::PluginDescription::isInstrument` — effects are never offered — and on this app's OWN VST3/AU
build, matched against `synth::branding::kProductName` / `kCompanyName` rather than a re-typed
literal, so it can never offer to host itself. The library sidebar goes through a different
collector and is unaffected.

Opening the picker (`buildAddTrackMenu()`, called by `openAddTrackMenu()` before it flattens the result,
and the headless test seam for inspecting the built `juce::PopupMenu`'s contents) calls
`TrackHeaderHost::ensureInstrumentPluginsScanned()` first, which `MainComponent` wires straight to
its existing `maybeStartEagerPluginScan()` — the SAME hosted-mode-guarded entry point the eager
startup scan and the library sidebar's manual "Scan for plugins..." row use, never a second scan
trigger.

An empty or still-in-progress list shows one disabled row — "Scanning for plugins..." or "No
instrument plugins found" (`TrackHeaderHost::isPluginScanInProgress()`) — instead of an empty
submenu. A same-named product built as both VST3 and AU is never shown as two identical rows: every
real entry's label ALWAYS carries its format in parentheses, using the sidebar's own short form
("Massive (VST3)" / "Massive (AU)"), never a bare name.

Choosing one calls `TrackHeaderHost::addInstrumentPluginTrack(identity)` — see
[`docs/mixer/mixer.md`](../mixer/mixer.md#a-hosted-plugin-as-the-instrument) for what it builds, why the
load has to be asynchronous, and how a document replaced mid-load (New Patch, Open, Load preset) is
handled.

## Add Marker

**Add Marker** sits below a separator because it is **not a track**: it adds no header row and no
graph node, it drops a flag on the ruler ([ruler](ruler.md#markers)). It shares this menu because
`"+ Track"` is where a user reaches for "add something to the arrangement", and a second button for
one item would not earn its pixels. It is handled *before* `applyAddTrackMenuChoice`'s
`TrackHeaderHost` null-check — a marker is document data with nothing behind it to wire — and calls
`addMarkerAtPlayhead()`.

## Adding a MIDI track

ONE compound undo step (`AppUndoManager::recordCombinedChange`, graph plus timeline in a single
transaction, so one Cmd+Z removes all of it and redo restores it with the same node uuid):

1. Add a `Midi` track named `Track N` — **the doc side goes first**. `TimelineDoc::addTrack` refuses
   past `kMaxTracks`, and a node created before that refusal is known stays in the graph with no
   track to play through: `recordCombinedChange` *records* a mutation, it does not roll one back. A
   track with no binding yet is never flagged orphaned, so the intermediate state is inert.
2. Create a `Track In` node through `AIStateMapper::createModule`, so it round-trips through
   `graphToJSON` / `applyJSONToGraph` — that is how undo, redo and `.agsproj` reproduce it — assign
   a fresh uuid and mirror it into the processor with `ModuleBase::setNodeUuid`, and place it at
   the canvas' left edge below every existing module, with x floored at `LayoutUtil::kMacroHullSideOutset` so the
   channel macro's open hull starts at x >= 0 (`GraphEditor::findLeftEdgeSlotBelowModules`). An AI edit plan's
   track uses the same slot while the canvas has no cards; the existing modules then block through their model
   rects ([where things land](../ai/timeline-ops.md#where-things-land)).
3. **Auto-wire only when unambiguous.** If the patch contains **exactly one** MIDI-driven
   instrument — module type `Poly MIDI`, `Oscillator`, `Wavetable`, `Sampler`, `Sequencer` or
   `Poly Sequencer`; MIDI *sources* (`Track In`, `External MIDI`, `MIDI Keyboard`) are excluded —
   connect `Track In -> that node` on the MIDI channel. With none or several, no wire is drawn: a
   chip that reads "bound" over an unwired node is fine, the cable is the user's to draw, whereas
   guessing wrong plays the track through the wrong instrument. `acceptsMidi()` cannot be the rule
   — `ModuleBase` returns `true` for every module in the app.
4. Bind the track to the new node's uuid and give it the palette colour for its index.

## Adding an Audio track

The Audio entry builds a **whole mixer channel**, not just a `Track Audio` node — see
[`docs/mixer/mixer.md`](../mixer/mixer.md#building-a-channel) for the full design. Steps 1 and 2
are the same as the MIDI entry (doc side first, so `kMaxTracks` refuses before any node is created;
factory-created node, uuid minted and mirrored, placed at the left edge), but everything downstream
of step 2 differs, and it is all still ONE undo step —
`AppUndoManager::recordGraphTimelineAndMacroChange`, which extends `recordCombinedChange`'s
graph+timeline transaction with a third domain, the macro set:

1. `createTrackAudioNode(wireDirectlyToMasterBus=false)` creates the `Track Audio` node **unwired**.
   The direct-to-master-bus auto-wire is now only `createAndBindTrackInNode()`'s behaviour — its ad
   hoc single-node rebind from the binding chip.
2. `synth::buildDefaultAudioChannel` (Core, `Source/Mixer/ChannelFlows/ChannelFlows.h`) wires the
   node into the factory default chain — `Track Audio -> Gate (bypassed) -> Parametric EQ (bypassed)
   -> Compressor (bypassed) -> Channel Strip (Stereo)` — then splices Master (`synth::spliceMasterNode`,
   reusing the existing singleton after the first channel; the same "Rec Tap when spliced, else Audio
   Output" target the old direct wire used) and wires the strip into Master's Mix input.
3. `GraphEditor::addMacroForMembers` boxes `{Track Audio, Gate, EQ, Compressor, Channel Strip}` into ONE
   collapsed macro named after the track. **Master stays outside the macro.** Core builds the
   Strip → Master cable as a plain graph edge; the app then moves it behind ONE output port of the macro
   (`GraphEditor::routeChannelOutputThroughMacroPort`, a single stereo jack, or two with the Split
   Left/Right jacks preference), so the track's sound visibly leaves through its own card — see
   [`docs/mixer/mixer.md`](../mixer/mixer.md#the-factory-default-chain). The Mix-vs-Direct classification
   `spliceMasterNode` does looks through macro ports to the strip behind them.
4. Bind the track to the `Track Audio` node's uuid and give it the palette colour for its index.

`GraphEditor::updateComponents()` runs **inside** the transaction's mutation, not after, so
`MacroSet::retainOnly()` sees every node above still alive when it reconciles macro membership.

## Adding an Instrument track

The MIDI-track mirror of the Audio entry: a `Track In` feeding a chosen instrument, then the same
factory default chain — see [`docs/mixer/mixer.md`](../mixer/mixer.md#an-instrument-track) for the
full design.

The picker offers exactly the audio-producing MIDI instruments (**Oscillator**, **Wavetable**,
**Sampler**), deliberately not every module the MIDI entry's own auto-wire search recognises as
"MIDI-driven": Poly MIDI outputs CV/gate and Sequencer/Poly Sequencer generate MIDI, none of them
audio. One undo step (`AppUndoManager::recordGraphTimelineAndMacroChange`,
`MainComponent::addInstrumentTrack`):

1. Add a `Midi` track (doc side first, same `kMaxTracks` ordering reason as every other entry).
   **This stays a `TrackKind::Midi` track rather than a new kind**: it is exactly what the MIDI
   entry's own auto-wire produces once a cable is drawn by hand — this flow just draws that cable
   and builds the channel automatically. See [`docs/mixer/mixer.md`](../mixer/mixer.md#channels-follow-audio-not-tracks)'s table note.
2. Create the `Track In` node (same factory, uuid and placement idiom as the MIDI entry), then the
   chosen instrument to its right, and wire `Track In -> instrument` on the MIDI channel — always
   unambiguous, since the instrument was just created for this track alone.
3. A poly instrument's raw ch0-7 (up to 8 simultaneous voices) cannot feed
   `buildDefaultAudioChannel` directly, which wants one stereo pair, so
   `synth::addVoiceMixerForPolyInstrument` (Core, `ChannelFlows.h`) sums them into a Voice Mixer
   first when the instrument's own `poly` parameter is on ([`docs/mixer/mixer.md`](../mixer/mixer.md#mono-and-stereo)). A
   factory-created instrument defaults to poly OFF, so this is a no-op on the golden path.
4. For an **Oscillator** or **Wavetable** instrument ([`docs/mixer/mixer.md`](../mixer/mixer.md#envelope-and-vca-for-a-raw-instrument)): neither has an
   envelope of its own, so a held or released note drones forever.
   `synth::addEnvelopeAndVCAForRawInstrument` inserts an ADSR (gated by the same Track In MIDI as
   the instrument, forced non-poly) driving a VCA (also forced non-poly) ahead of the rest of the
   chain — AFTER the Voice Mixer from step 3, never before it. A no-op for **Sampler**, which
   already has its own one-shot playback envelope.
5. `synth::buildDefaultAudioChannel` wires the instrument (or the Voice Mixer or VCA, whichever
   step 3 or 4 last produced) into the same `Gate (bypassed) -> Parametric EQ (bypassed) -> Compressor
   (bypassed) -> Channel Strip (Stereo)` chain the Audio entry uses, then splices Master. A split-block source
   (Oscillator or Wavetable, whose right leg is never ch1) passes its own
   `ModuleBase::rightAudioLegChannel()` — or `VCAModule::kRightBase`, once step 4 has run — as
   `buildDefaultAudioChannel`'s `sourceRightChannel` parameter instead of the ch1 default.
6. `GraphEditor::addMacroForMembers` boxes `{Track In, instrument, [Voice Mixer if any], [ADSR+VCA
   if Oscillator/Wavetable], Gate, EQ, Compressor, Strip}` into ONE collapsed macro named after the
   track, the same way the Audio entry's macro is built — Master stays outside it, for the same
   reason.
7. Bind the track to the `Track In` node's uuid and give it the palette colour for its index.

The same build also runs from a model's `addInstrumentTrack` timeline op, through
`MainComponent::buildInstrumentTrackBody` with the op's exact track name and without the default
track preset lookup, plus optional effect **inserts** placed after the step-3/4 envelope stage and
before the Gate (each a macro member) — see
[`docs/ai/timeline-ops.md`](../ai/timeline-ops.md#addinstrumenttrack). On a failure partway, that
shared body removes every node it added and the doc track before returning.

## Duplicate a track

**Cmd+D on a focused track row**, or **Duplicate Track** in the row's right-click menu (it names the shortcut), adds a
copy directly below it with its modules, clips, notes and automation. The key is the rebindable
`timelineDuplicateFocusedTrack` (Timeline category, default Cmd+D). It shares the chord with the General
`duplicateSelection` because conflicts are per category and the row's own `keyPressed()` claims the key before the
command layer sees it: Cmd+D anywhere else (canvas, clips, piano roll) keeps its meaning. The Automation section header
has nothing to copy and lets the key bubble.

`MainComponent::duplicateTrack` (`MainComponentTrackDuplicate.cpp`) runs ONE graph + timeline + macro undo step, then
the reconcile pass:

1. **Which modules.** The nodes the track plays and no other track does (`resolveAutomationOwners`, the rule
   [automation](automation.md#which-track-a-lane-lands-on) uses), minus the output dock (Master and everything after it).
   A node two tracks reach stays shared, and so does a free-standing patch that only feeds the output dock: a node is
   copied only when cables through the track's own nodes link it to the track's start without passing through Master.
2. **Copy.** The same `SnippetManager::extractSnippet` / `insertSnippet` pair copy-paste uses
   ([snippets-clipboard](../layout/snippets-clipboard.md#copy-paste-and-duplicate)): new node ids and uuids, parameters and
   extra state carried, cables between copied modules and their macros (with ports and nesting) come across, placed one
   gap below the original's lowest card. `insertSnippet`'s `outCopies` hands back original id -> copy id.
3. **Boundary cables.** A paste drops wires that left the selection; a duplicated track must not, so every cable between
   a copied module and a module that was not copied is re-created on the copy: into Master (the copy's channel is routed
   like a new track's), into a shared bus, and from a shared source. A modulation routing into a copied module from a
   shared modulator is re-created through `AudioEngine::addModRouting` with its amount. A channel macro named after the
   track is renamed to the copy's name so two mixer columns never read the same.
4. **Timeline.** `TimelineDoc::duplicateTrack` inserts the copy below the source with fresh ids on the track, clips, notes
   and lanes; the binding and each lane are re-pointed through the original-uuid -> copy-uuid map. A lane whose module is
   shared stays with the original (a (module, parameter) pair carries one lane doc-wide). The copy is named "<name> copy",
   takes the next palette colour, and is never soloed or armed. An unbound track is copied as clips alone.

Focus follows the copy (`TimelinePanelComponent::focusTrackRow`), so Cmd+D again duplicates the copy. The new row emerges
from its source's slot while the rows below glide down one slot (140 ms through the same `ReorderDragAnimator` an undo
uses, armed by `armTrackDuplicateGlide`); under Reduce Motion, or off-screen, it lands at once. At `kMaxTracks` nothing
happens and the status bar says so.

Tests: `Tests/App/MainComponent/MainComponentDuplicateTrackTests.cpp` (the real key through the row; new ids, remapped
cables, Master routing, clips, notes, lane; one undo and redo),
`Tests/UI/Timeline/TimelineTrackDuplicateTests.cpp` (key, rebind, menu, glide),
`Tests/Timeline/TimelineDoc/TimelineDocTracksTests.cpp` (`duplicateTrack`).
