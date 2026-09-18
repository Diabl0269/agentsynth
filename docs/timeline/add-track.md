# Adding Tracks

The `"+ Track"` button at the top of the timeline panel's header column, and every flow it starts.
The header rows it creates are [tracks](tracks.md); the channel chains it builds are
[`mixer_implementation.md`](../mixer_implementation.md).

## The menu

`"+ Track"` carries the tooltip *"Add a MIDI or Audio track"* so the menu is not a surprise, and
opens:

- **MIDI Track** / **Audio Track**
- an **Instrument Track** submenu — **Oscillator** / **Wavetable** / **Sampler**, poly variants,
  then a **Plugin** sub-submenu
- the saved track presets for each kind
- a separator, then **Add Marker**

Menu ids are `TimelinePanelComponent::kAddMidiTrackMenuId` / `kAddAudioTrackMenuId` /
`kAddInstrumentOscillatorMenuId` / `kAddInstrumentWavetableMenuId` / `kAddInstrumentSamplerMenuId`
/ `kAddMarkerMenuId`. The MIDI and Audio entries land on `TrackHeaderHost` (`addMidiTrack()` /
`addAudioTrack()`); each Instrument submenu entry calls `addInstrumentTrack(instrumentModuleType)`
with its module type string.

`TimelinePanelComponent::applyAddTrackMenuChoice(id)` is the headless seam for all of them — the
same split the binding and context menus use, since a `juce::PopupMenu` never runs in the test
binary. `MainComponent::simulateAddMidiTrackClick()` / `simulateAddAudioTrackClick()` /
`simulateAddInstrumentTrackClick(menuId)` call straight into it. `openAddTrackMenu()` is the
`protected virtual` that creates the real window — see
[ruler](ruler.md#opening-a-menu-is-a-protected-virtual) for why that seam exists.

## Menu options resolve against a build-time snapshot

Every real plugin entry's id is `kAddInstrumentPluginMenuIdBase + index`, where `index` indexes
`TimelinePanelComponent::collectInstrumentPluginMenuOptions()`'s result AT BUILD TIME — captured
into the member `instrumentPluginMenuSnapshot_` the moment the menu is built by
`buildAddTrackMenu()`. `applyAddTrackMenuChoice` resolves strictly against that snapshot, never by
re-collecting.

**Why, and why this differs from the automation strip's lane picker.** The known-plugin list
backing this menu is mutated by `PluginScanService::runScan` on a BACKGROUND thread and re-sorted
by name in `MainComponent::getInstrumentPluginOptions()`. A scan completing between the menu
opening and the click landing can otherwise change what index N means, silently resolving the click
against a plugin the menu never actually showed there — the live symptom is a menu showing
"Massive", clicking it, and getting a Sampler track instead. The automation strip's lane picker
(`collectAutomationLaneOptions` / `applyAutomationLaneMenuChoice`) deliberately re-runs its
collector at click time, because an automation lane is document data mutated only on the message
thread, so that is safe.

The track-preset lists follow the same build-time-snapshot rule
(`audioTrackPresetMenuSnapshot_` / `instrumentTrackPresetMenuSnapshot_`), since a preset can be
saved or deleted between the menu opening and the click landing.

## Track presets

Below the Instrument submenu, `"+ Track"` lists every saved preset for each track kind — own
**Audio** and **Instrument** submenus, one item per preset via
`synth::TrackPresetManager::listTrackPresets`, ids `kAddTrackPresetAudioMenuIdBase` /
`kAddTrackPresetInstrumentMenuIdBase` — then a final **"Insert Track Preset from File..."** entry
that loads one saved anywhere on disk.

This is a **separate** path from the plain MIDI/Audio/Instrument entries above: those consult only
each type's *default* preset (Preferences → Mixer), while this submenu can insert ANY saved preset
regardless of which one is currently the default. See [`mixer.md`](../mixer.md) §5.7.

## The Plugin sub-submenu

The Instrument submenu's **Plugin** sub-submenu lists the scanned INSTRUMENT hosted plugins
(`TrackHeaderHost::getInstrumentPluginOptions()`), filtered on
`juce::PluginDescription::isInstrument` — effects are never offered — and on this app's OWN VST3/AU
build, matched against `synth::branding::kProductName` / `kCompanyName` rather than a re-typed
literal, so it can never offer to host itself. The library sidebar goes through a different
collector and is unaffected.

Opening the menu (`buildAddTrackMenu()`, called by `openAddTrackMenu()` before it shows the result,
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
[`mixer_implementation.md`](../mixer_implementation.md)'s P9-3h entry for what it builds, why the
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
   the canvas' left edge below every existing module
   (`GraphEditor::findLeftEdgeSlotBelowModules`).
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
[`mixer_implementation.md` item 2](../mixer_implementation.md) for the full design. Steps 1 and 2
are the same as the MIDI entry (doc side first, so `kMaxTracks` refuses before any node is created;
factory-created node, uuid minted and mirrored, placed at the left edge), but everything downstream
of step 2 differs, and it is all still ONE undo step —
`AppUndoManager::recordGraphTimelineAndMacroChange`, which extends `recordCombinedChange`'s
graph+timeline transaction with a third domain, the macro set:

1. `createTrackAudioNode(wireDirectlyToMasterBus=false)` creates the `Track Audio` node **unwired**.
   The direct-to-master-bus auto-wire is now only `createAndBindTrackInNode()`'s behaviour — its ad
   hoc single-node rebind from the binding chip.
2. `synth::buildDefaultAudioChannel` (Core, `Source/Mixer/ChannelFlows/ChannelFlows.h`) wires the
   node into the factory default chain — `Track Audio -> Parametric EQ (bypassed) -> Compressor
   (bypassed) -> Channel Strip (Stereo)` — then splices Master (`synth::spliceMasterNode`, reusing
   the existing singleton after the first channel; the same "Rec Tap when spliced, else Audio
   Output" target the old direct wire used) and wires the strip into Master's Mix input.
3. `GraphEditor::addMacroForMembers` boxes `{Track Audio, EQ, Compressor, Channel Strip}` into ONE
   collapsed macro named after the track. **Master stays outside the macro**, and the
   Strip → Master cable is left a plain graph edge, deliberately never a macro port — see
   `mixer_implementation.md`'s item 2 for why: the Mix-vs-Direct classification `spliceMasterNode`
   does would break behind a `MacroOutlet`.
4. Bind the track to the `Track Audio` node's uuid and give it the palette colour for its index.

`GraphEditor::updateComponents()` runs **inside** the transaction's mutation, not after, so
`MacroSet::retainOnly()` sees every node above still alive when it reconciles macro membership.

## Adding an Instrument track

The MIDI-track mirror of the Audio entry: a `Track In` feeding a chosen instrument, then the same
factory default chain — see [`mixer_implementation.md` item 2](../mixer_implementation.md) for the
full design.

The picker offers exactly the audio-producing MIDI instruments (**Oscillator**, **Wavetable**,
**Sampler**), deliberately not every module the MIDI entry's own auto-wire search recognises as
"MIDI-driven": Poly MIDI outputs CV/gate and Sequencer/Poly Sequencer generate MIDI, none of them
audio. One undo step (`AppUndoManager::recordGraphTimelineAndMacroChange`,
`MainComponent::addInstrumentTrack`):

1. Add a `Midi` track (doc side first, same `kMaxTracks` ordering reason as every other entry).
   **This stays a `TrackKind::Midi` track rather than a new kind**: it is exactly what the MIDI
   entry's own auto-wire produces once a cable is drawn by hand — this flow just draws that cable
   and builds the channel automatically. See `docs/mixer.md` §5.2's table note.
2. Create the `Track In` node (same factory, uuid and placement idiom as the MIDI entry), then the
   chosen instrument to its right, and wire `Track In -> instrument` on the MIDI channel — always
   unambiguous, since the instrument was just created for this track alone.
3. A poly instrument's raw ch0-7 (up to 8 simultaneous voices) cannot feed
   `buildDefaultAudioChannel` directly, which wants one stereo pair, so
   `synth::addVoiceMixerForPolyInstrument` (Core, `ChannelFlows.h`) sums them into a Voice Mixer
   first when the instrument's own `poly` parameter is on (`docs/mixer.md` §5.4/§5.8). A
   factory-created instrument defaults to poly OFF, so this is a no-op on the golden path.
4. For an **Oscillator** or **Wavetable** instrument (`docs/mixer.md`'s P9-3i entry): neither has an
   envelope of its own, so a held or released note drones forever.
   `synth::addEnvelopeAndVCAForRawInstrument` inserts an ADSR (gated by the same Track In MIDI as
   the instrument, forced non-poly) driving a VCA (also forced non-poly) ahead of the rest of the
   chain — AFTER the Voice Mixer from step 3, never before it. A no-op for **Sampler**, which
   already has its own one-shot playback envelope.
5. `synth::buildDefaultAudioChannel` wires the instrument (or the Voice Mixer or VCA, whichever
   step 3 or 4 last produced) into the same `Parametric EQ (bypassed) -> Compressor (bypassed) ->
   Channel Strip (Stereo)` chain the Audio entry uses, then splices Master. A split-block source
   (Oscillator or Wavetable, whose right leg is never ch1) passes its own
   `ModuleBase::rightAudioLegChannel()` — or `VCAModule::kRightBase`, once step 4 has run — as
   `buildDefaultAudioChannel`'s `sourceRightChannel` parameter instead of the ch1 default.
6. `GraphEditor::addMacroForMembers` boxes `{Track In, instrument, [Voice Mixer if any], [ADSR+VCA
   if Oscillator/Wavetable], EQ, Compressor, Strip}` into ONE collapsed macro named after the
   track, the same way the Audio entry's macro is built — Master stays outside it, for the same
   reason.
7. Bind the track to the `Track In` node's uuid and give it the palette colour for its index.
