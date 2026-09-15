# Mixer — Implementation order and Tests

One topic per doc (root `CLAUDE.md` "Code structure"): [`docs/mixer.md`](mixer.md) holds the design
(what a channel is, the link rule, inserts, track presets, panel placement); this doc holds the
build log — dependency order, what landed and how, and the test list for each P9 item. Section/item
numbers below match the numbering `docs/mixer.md` and other docs already cite as "mixer.md §8
item N" — those citations now point here instead, same numbers.

---

## 8. Implementation order and Tests

Main line, in dependency order:

1. **P9-2 (T172) — `ChannelStrip` + `Master` + the solo gate.** Engine only. **DONE.** How it
   landed (module detail in [`docs/modules.md`](modules.md), engine detail in
   [`docs/architecture.md`](architecture.md) § Mixer solo gate):
   - *Strip layout*: 5 raw channels a side, Left ch0 / Right `kRightBase` = 4, ch1–3 reserved;
     params `gain` (dB), `pan`, `muted`; shape + solo in extra state.
   - *Master layout*: Mix L/R on ch0/1, Direct L/R on ch2/3; Direct summed in before the fader;
     bypass is a unity sum that keeps Direct.
   - *§5.3's open detail, resolved*: an engine-owned atomic count, recounted on the message
     thread by scanning the graph inside `publishTimeline` (so a deleted/undone soloed strip
     cannot leave the mix stuck), carried per render pass on `TransportService` like the
     input-monitoring flag.
   - *Solo applies to a bypassed strip too* — bypass is the strip's own gain/pan going dry, the
     gate is layered on top; otherwise the strip's card bypass would leak into a soloed mix.
   - *The splice* is `synth::ensureMasterNode` (Core, `Source/Mixer/MasterSplice.h`); nothing
     calls it yet — P9-3's channel creation does.
   - `Tests/Mixer/ChannelStripTests.cpp`: pan balance law is unity at centre for both mono and stereo
     shapes; bypass/mute follow the two-branch contract; shape is fixed at construction and
     rejects a later width change.
   - `Tests/Mixer/MixerSoloTests.cpp`: soloing one strip silences every other non-soloed strip and the
     Direct input; un-soloing the last soloed strip restores every other strip; solo never mutates
     another strip's mute parameter; `Master` splice is one undo step and undoes cleanly.

2. **P9-3 (T173) — Channel creation flows.** Audio track auto-channel, the Instrument track,
   MIDI-track auto-channel-on-connect (§5.2's main workflow, Preferences-gated), "Make channel",
   "Create channels" for existing projects, the factory default chain (EQ -> Compressor, bypassed).
   - Tests: an audio/instrument track creation produces exactly one channel in one undo step; a
     MIDI track wired to an unchanneled instrument auto-creates a channel in one undo step when
     the preference is ON, and creates none when it is OFF; "Create channels" on a project with N
     channel-less tracks is one undo step that creates N channels.
   - **T173a (audio track) — DONE.** "+ Track -> Audio Track" builds the whole channel — Track
     Audio -> Parametric EQ (bypassed) -> Compressor (bypassed) -> Channel Strip (Stereo) ->
     Master (Mix) — in ONE undo step (`AppUndoManager::recordGraphTimelineAndMacroChange`), via a
     new Core helper `synth::buildDefaultAudioChannel` (`Source/Mixer/ChannelFlows/ChannelFlows.h`) that splices
     Master (`synth::spliceMasterNode`, reusing the existing singleton after the first channel) and
     wires the strip into it. `{Track Audio, EQ, Compressor, Strip}` are boxed into ONE collapsed
     macro named after the track (`GraphEditor::addMacroForMembers`) — **Master stays outside the
     macro, and the Strip -> Master cable is left a plain graph edge, deliberately never a macro
     port**: `spliceMasterNode`/`ensureMasterNode` classify a re-routed feed as Mix vs Direct by
     checking whether the connection's SOURCE NODE is itself a `ChannelStripModule`; a
     `MacroOutlet` sitting between the strip and Master would make the source node a `MacroOutlet`
     instead and defeat that check, which the "Create channels" flow (T173e, DONE — see below,
     "N channel-less tracks") depends on.
   - **T183 (instrument track) — DONE.** "+ Track -> Instrument" opens a submenu of audio-producing
     MIDI instruments (Oscillator, Wavetable, Sampler — deliberately NOT every
     `isMidiInstrumentType()` member: Poly MIDI/Sequencer/Poly Sequencer generate CV/MIDI, not
     audio) and builds Track In -> the chosen instrument -> the same
     `synth::buildDefaultAudioChannel` chain T173a uses, in ONE undo step
     (`MainComponent::addInstrumentTrack`). `{Track In, instrument, [Voice Mixer if poly], EQ,
     Compressor, Strip}` are boxed into one collapsed macro the same way T173a's are; Master stays
     outside it for the same `spliceMasterNode` reason. `buildDefaultAudioChannel` gained a
     trailing `sourceRightChannel` parameter (default 1, Track Audio's contiguous pair unchanged)
     so a split-block source can pass its own `ModuleBase::rightAudioLegChannel()` instead of
     assuming ch1 — Oscillator/Wavetable's right leg is never ch1 (Source/Modules/CLAUDE.md). A new
     `synth::addVoiceMixerForPolyInstrument` (§5.4/§5.8's "poly output needs a Voice Mixer ahead of
     the strip" rule) sums a poly instrument's ch0-7 into a Voice Mixer first — gated on the
     instrument's own live `poly` parameter, never forced on: a factory-created instrument defaults
     to poly OFF, so this branch is a no-op on the golden path today and exists for whenever it
     isn't (`Tests/ChannelFlowTests.cpp`'s `PolyInstrumentGetsVoiceMixerAheadOfStripAndFeedsTheChannel`
     exercises it directly). See §5.2's table note for the `TrackKind::Midi` scope decision.
     `Tests/ChannelFlowTests.cpp`.
   - **P9-3i (Oscillator/Wavetable envelope) — DONE.** Oscillator and Wavetable have no envelope of
     their own — a held (or even released) note droned forever. "+ Track -> Instrument ->
     {Oscillator/Wavetable}" now inserts an ADSR + VCA ahead of the rest of the chain:
     `Track In --MIDI--> ADSR --Env--> VCA's Gain CV`, `chainSource -> VCA Audio -> EQ`
     (`synth::addEnvelopeAndVCAForRawInstrument`, `Source/Mixer/ChannelFlows/ChannelFlows.h`/`.cpp`). Inserted
     AFTER any Voice Mixer stage, never before it, and both nodes are forced non-poly regardless of
     the instrument's own `poly` parameter: `ADSRModule`'s poly branch is CV-gate-only (it never
     reads the MIDI note-on/off fallback that drives its non-poly branch), so a poly ADSR fed only
     Track In's MIDI would output a permanent zero envelope. ADSR's `sustain` (stock default 0.0)
     is overridden to 0.7 so a held note actually sustains instead of plucking-and-dying after
     ~0.25s; VCA's `gain` (stock default 0.5) is overridden to 1.0 so the envelope alone governs
     level. Sampler is untouched — it already has its own one-shot playback envelope, out of scope.
     `{Track In, instrument, [Voice Mixer if poly], ADSR, VCA, EQ, Compressor, Strip}` join the same
     one collapsed macro. See `Tests/ChannelFlowTests.cpp`'s
     `InstrumentTrackOscillatorEnvelopeActuallySilencesAfterNoteOff` for the render-level proof (not
     just topology) that the envelope actually gates audio.
   - **P9-3j (poly Oscillator/Wavetable envelope) — DONE (FRO46).** P9-3i's ADSR+VCA were forced
     non-poly even when the instrument's own `poly` parameter was on: `ADSRModule`'s poly branch is
     CV-gate-only, it never reads the MIDI note-on/off fallback that drives its non-poly branch, so
     a poly ADSR fed only raw MIDI would output a permanent zero envelope. Fixed by inserting a
     **Poly MIDI** node (the codebase's existing per-voice MIDI-to-CV converter, §"Poly MIDI Module"
     in `docs/modules.md`) between Track In and the instrument instead of raw MIDI, when the
     instrument's `poly` parameter is on at instrument-track creation time — whether set
     programmatically or, since FRO48/P9-3k below, via the "(Poly)" menu entry (same "poly handled
     correctly wherever it arises" precedent T183's `addVoiceMixerForPolyInstrument` established):
     ```
     Track In --MIDI--> Poly MIDI --Pitch(ch0-7)--> instrument's poly Pitch CV in (ch0-7)
                         Poly MIDI --Gate(ch8-15)--> ADSR's poly Gate CV in (ch0-7)
     ADSR poly Env (ch0-7) --> VCA's poly Gain CV in (ch8-15, VCAModule::kPolyCVBase)
     instrument's poly Audio L (ch0-7) --> VCA's poly Audio L in (ch0-7)
     ```
     Both ADSR and VCA are now genuinely poly (`synth::addPolyEnvelopeAndVCAForInstrument`,
     `Source/Mixer/ChannelFlows/ChannelFlows.h`/`.cpp`), giving each voice its own independent envelope instead
     of one shared mono envelope gating the whole poly-voice sum. No separate Voice Mixer is
     inserted for this case — the poly VCA already sums all 8 gated voices to a stereo-shaped pair
     itself (ch0 = left sum, ch1 = its own legacy duplicate), so `addVoiceMixerForPolyInstrument` is
     skipped entirely when this branch fires; it still fires for a poly instrument this doesn't
     apply to (e.g. a poly Sampler). The instrument's R-octet is deliberately NOT wired into the
     VCA's own Audio R poly block (ch16-23) — same known stereo limitation
     `addVoiceMixerForPolyInstrument`'s own comment documents for the non-envelope poly path.
     `{Track In, instrument, Poly MIDI, ADSR, VCA, EQ, Compressor, Strip}` join the same one
     collapsed macro. See `Tests/ChannelFlowTests.cpp`'s
     `PolyEnvelopeAndVCAWiresPerVoicePitchGateAndAudioWithNoVoiceMixer` for the wiring proof and
     `Tests/Modules/PolyMidiModuleTests.cpp`'s `PolyMidiToAdsrToVcaTest.ReleasingOneVoiceLeavesAnother-
     HeldVoiceUntouched` for the render-level proof that releasing one voice's note leaves another
     held voice's envelope untouched — the thing a single shared mono envelope could never do.
   - **P9-3k (poly Oscillator/Wavetable UI entry) — DONE (FRO48).** The "+ Track > Instrument" menu
     now offers "Oscillator (Poly)" and "Wavetable (Poly)" entries (Sampler has no poly parameter,
     so it has no poly entry) that set the new instrument's "poly" AudioParameterBool via the new
     `synth::setProcessorPoly()` write counterpart to `isProcessorPoly()` before the poly-envelope
     branch check runs, making P9-3j's poly-envelope auto-wire reachable from the UI for the first
     time. See `ChannelFlowTest.AddInstrumentTrackMenuOscillatorPolyWiresPolyEnvelopeAndVCA` /
     `...WavetablePolyWiresPolyEnvelopeAndVCA` in `Tests/ChannelFlowTests.cpp`.
   - **P9-3h (hosted plugin as the instrument) — DONE (FRO42).** "+ Track -> Instrument" gained a
     "Plugin" sub-submenu listing the scanned INSTRUMENT plugins (`juce::PluginDescription::
     isInstrument`; effects are filtered out and never offered here) — opening the menu calls
     `MainComponent::maybeStartEagerPluginScan()` (FRO44's shared, hosted-mode-guarded entry point;
     never a parallel scan trigger) so the list is populated even on the FIRST open, and an empty/
     still-scanning list shows one disabled "Scanning for plugins..." / "No instrument plugins
     found" row instead of an empty submenu. Choosing a plugin builds the exact same Track In ->
     instrument -> default chain (EQ bypassed -> Compressor bypassed -> Channel Strip) -> Master
     flow T183 built, boxed into one collapsed macro, as ONE undo step — except the instrument is a
     hosted `HostedPluginModule` instance rather than a factory module, and the load is
     ASYNCHRONOUS: `MainComponent::addInstrumentPluginTrack` stages a bare Hosted Plugin module OFF
     the graph, starts its load, and only opens the undo transaction (via the SAME shared tail
     `MainComponent::buildInstrumentTrackAndChain` now factors `addInstrumentTrack` through) once
     `HostedPluginModule::onLoadCompleted` reports success — a new completion hook that fires once
     per load attempt covering all three exits (publish, an outright backend failure, and the
     over-max refusal inside `publishInstance()`, none of which `onInstancePublished`/
     `onInstanceChanged` alone would catch). A failed or refused load touches neither the graph nor
     the undo stack; the staged module (owned by `MainComponent::pendingInstrumentPluginLoads_`,
     never by a `shared_ptr` looped back through its own `onLoadCompleted`, which would keep the
     object alive forever) is simply torn down on the next message-loop turn. No P9-3i ADSR+VCA is
     added — a hosted synth has its own envelope — which the shared tail gets for free: it keys the
     Oscillator/Wavetable/poly branches off the instrument NODE's own `getName()`/"poly" parameter,
     and a `HostedPluginModule` is named "Hosted Plugin" and declares no "poly" parameter, so every
     one of those branches falls through to the plain path automatically. `ModuleBase::
     rightAudioLegChannel()` — read only AFTER the load completes, since the module's real channel
     count isn't known before then — gained a `HostedPluginModule` override deriving from the
     published instance's real output count (ch1 once there are 2+ outputs; ch0 duplicated onto both
     legs for a genuinely mono instance; the base class's `hasDualIOParameter()`-gated default would
     have read -1 forever, since this module never registers a Dual I/O parameter). See
     `Tests/Plugin/HostedPluginTests.cpp`'s `OnLoadCompletedFires*`/`RightAudioLegChannelFollows...` and
     `Tests/ChannelFlowTests.cpp`'s `PluginInstrumentTrack*` — the latter drives the exact same
     `applyAddTrackMenuChoice`/menu-id seam the T183 tests use.

     **Review fixes on top of the above (same FRO42/P9-3h):** (1) the picker's Plugin sub-submenu
     resolves a click against a SNAPSHOT of the options list captured when the menu was built
     (`TimelinePanelComponent::instrumentPluginMenuSnapshot_`), never by re-running the collector at
     click time — a background `PluginScanService::runScan` finishing between open and click could
     otherwise resolve the click against a different plugin than the one the menu showed (see
     `docs/timeline_panel_tracks.md` §3 for the full mechanism and the live repro this fixed). (2)
     Every entry's label always carries its format — "Massive (VST3)" / "Massive (AU)" — so a VST3
     and an AU build of the same product never show as two identical rows. (3) The picker excludes
     this app's own VST3/AU build (matched against `synth::branding::kProductName`/`kCompanyName`),
     so it never offers to host itself; the library sidebar is a separate collector and stays
     unfiltered. (4) A load still in flight when the document is replaced (New Patch/Open/Load
     preset, all via `guardUnsavedChanges`) is dropped rather than landing in the FRESH document:
     `MainComponent::documentGeneration_` is bumped once per replacement that actually proceeds
     (never on Cancel/a failed Save), `addInstrumentPluginTrack` captures it when the load starts,
     and `onLoadCompleted` compares it before building a track — a mismatch tears the staged module
     down the same deferred way a failed load already was, touching neither the graph nor the undo
     stack of the document that is live by then. See `Tests/ChannelFlowTests.cpp`'s
     `PluginInstrumentMenuChoiceResolvesAgainstSnapshotNotALaterRescan`,
     `PluginInstrumentMenuAppendsFormatLabelSoIdenticalNamesAreDistinguishable`,
     `PluginInstrumentMenuExcludesThisAppsOwnPluginBuild`, and
     `PluginInstrumentLoadCompletingAfterNewPatchIsDroppedNotAddedToTheFreshDocument`.
   - **T184 (MIDI-track auto-channel-on-connect) — DONE.** Dragging a MIDI cable from a Track In
     node (`ModuleType::TimelineMidiSource`) onto an instrument or macro whose audio reaches Audio
     Output/Rec Tap/Master's Direct bus with no `ChannelStrip` anywhere on that path auto-builds a
     channel there, as ONE undo step with the connection itself
     (`AppUndoManager::recordGraphAndMacroChange`, `GraphEditor::endConnectionDrag`). An instrument
     that already has a channel on its path gets nothing new — connecting a second MIDI track just
     wires straight in, same as any other MIDI-track-into-an-existing-instrument case (§5.2's
     table). Gated by a Preferences toggle, `mixerAutoCreateChannelOnConnect`, default **ON**; OFF
     restores exactly today's behaviour (wire freely, no channel appears).
     - *Core* (`Source/Mixer/ChannelFlows/ChannelFlows.h`/`.cpp`, no AppUI/GraphEditor/AppUndoManager
       dependency): `synth::findUnchanneledOutputFeeds(graph, start)` does a forward BFS from the
       just-connected node over audio AND MIDI edges — never expanding past a
       `ChannelStripModule` (already channeled: stop, no exit), a `RecordTapModule`, a
       `MasterModule`, or the `AudioGraphIOProcessor` named "Audio Output" (all three are
       terminals), and never traversing INTO a hidden `AttenuverterModule` (a mod/CV leg is not an
       audio-reaching-the-output path). An "exit" is an edge landing on Audio Output ch0/1, Rec Tap
       ch0/1, or Master's `kDirectLeft`/`kDirectRight` — `kMixLeft`/`kMixRight` are deliberately
       NOT exits, since only a `ChannelStripModule`'s own output legitimately lands there.
       `synth::buildChannelForFeeds(graph, exits, layout)` removes every exit edge FIRST (so
       `spliceMasterNode`'s own "sweep everything already feeding Audio Output/Rec Tap into Master"
       behaviour, run as part of building the chain when no Master exists yet, never re-captures an
       edge this call is about to own), then builds EQ(bypassed) -> Compressor(bypassed) -> Strip
       (Stereo) -> Master via the SAME internal builder `buildDefaultAudioChannel` now shares
       (`buildChannelChain`), generalized to accept arbitrary left/right feed lists instead of one
       fixed stereo pair (multiple feeds landing on the same side is fine — `AudioProcessorGraph`
       sums them).
     - *GraphEditor hook* (`Source/UI/Graph/GraphEditor/`): `endConnectionDrag` gates on the drag
       being MIDI and the real source node being `ModuleType::TimelineMidiSource`
       (`nodeIsTimelineMidiSource`), covering both the direct-jack path and the collapsed-macro-card
       "existing port jack" path — not the `createMacroPortFromDroppedCable` fallback (dropping a
       cable on a macro's body with no jack under it mints a port with no interior leg yet, so
       there is nothing to search from and T184 never applies there). One undo transaction wraps macro-port
       auto-creation (T148, if the drag also crosses a macro boundary), the connection itself, and
       the auto-channel build — `maybeAutoCreateMacroPortsForDrag` gained a trailing
       `recordUndo = true` parameter so a caller already inside its own transaction can pass
       `false` and hoist `updateComponents()` out to run once, after every mutation, instead of the
       nested-transaction double-repaint a naive wrap would produce (docs/macros_implementation.md §7 item 9 has
       the signature detail). Layout mirrors T173a/T183's own pattern: `estimateModuleSize()` per
       node type plus a 40px gap constant (`kAutoChannelCardGapX`, GraphEditorChannels.cpp's own copy
       of `MainComponentTrackCreation.cpp`'s `kChannelCardGapX` — duplicated rather than shared, since
       Core/UI layering keeps GraphEditor's sources from reaching into the MainComponent units).
       **Boxing rule:** the
       new EQ/Compressor/Strip join the SAME macro as the instrument only when every distinct exit
       source node is already an ORDINARY member of ONE common macro (not a port, not split across
       macros, not un-macroed) — otherwise the new chain nodes are left unboxed on the canvas
       rather than guessing which container they belong to.
     - Tests: `Tests/ChannelFlowTests.cpp` — Core-level (`ChannelFlowAutoChannelCore`):
       `FindUnchanneledOutputFeedsOnInstrumentToOutputFindsTwoExits`,
       `FindUnchanneledOutputFeedsOnStripChanneledInstrumentFindsZero`,
       `FindUnchanneledOutputFeedsNotReachingOutputFindsZero`,
       `FindUnchanneledOutputFeedsIgnoresModulationBranchAndSweepsTheUnrelatedPathOntoMasterDirect` (a mod/CV
       leg through a hidden Attenuverter is neither traversed nor counted, and an unrelated
       pre-existing direct-to-output feed gets correctly swept onto Master's Direct bus by the
       same-transaction `spliceMasterNode` splice, exactly like any other pre-existing feed would
       be), `BuildChannelForFeedsRemovesExitEdgesAndWiresThroughToANewMaster`,
       `BuildChannelForFeedsReusesAnExistingMasterAndClearsDirectFeeds`,
       `BypassedEQAndCompressorReportZeroLatencyAfterPrepare` (latency-zero gate: neither module
       calls `setLatencySamples`). Real-mouse-path (`ChannelFlowTest`, `MainComponent` fixture,
       synthesized `juce::MouseEvent`s driven straight into `ModuleComponent`, docs/testing.md's
       "test the real mouse path"): `AutoChannelOnConnect_ToggleOnBuildsOneChannelAsOneUndoStep`,
       `AutoChannelOnConnect_ToggleOffOnlyConnectsNoChannel`,
       `AutoChannelOnConnect_AlreadyChanneledInstrumentGetsNoNewStrip`,
       `AutoChannelOnConnect_NewChainNodesJoinTheInstrumentsExistingMacro`. Plus
       `Tests/UI/Settings/PreferencesSettingsTab/PreferencesSettingsTabGraphBehaviourTests.cpp`: default ON, persists `"0"`/`"1"` under
       `mixerAutoCreateChannelOnConnect` and round-trips, and pushes to a live `GraphEditor` via
       `setGraphEditor`/on toggle, mirroring every T148 toggle test exactly.
   - **T173e (existing projects, "Create channels") — DONE (FRO26).** The "+ Track" menu's new
     "Create Channels" entry (`TimelinePanelComponent::kCreateChannelsMenuId`) — see §5.13 for why
     it lives there. `GraphEditor::createChannelsForUnchanneledTracks(trackSourceNodeIds)` is a thin
     public wrapper: for each id, it calls the SAME private `maybeAutoCreateChannelAfterConnect`
     T184's `endConnectionDrag` hook already uses, reusing its exit-finding, chain-building, T187
     Master-relocation and same-macro boxing logic unchanged rather than a parallel implementation.
     `MainComponent::createChannelsForExistingTracks()` gathers every track's own bound node
     (`TimelineDoc::Track::bindingUuid`) first, then wraps the whole per-track sweep in ONE
     `AppUndoManager::recordGraphTimelineAndMacroChange` transaction, so N channel-less tracks
     becoming N new channels (sharing one `Master` after the first) is a single Cmd+Z.
     `hasTracksNeedingChannels()` — the same `synth::findUnchanneledOutputFeeds` query per track,
     short-circuiting on the first hit — backs both the menu's enabled state and the action's own
     no-op guard, so a project where every track already has a channel changes nothing and pushes
     no undo step. Both methods are non-pure `TrackHeaderHost` virtuals with inert defaults
     (`Source/UI/Timeline/TimelineTrackHeaderComponent.h`), the same pattern every other "+ Track" action
     uses, so existing test stubs keep compiling untouched.
     - Tests (`Tests/ChannelFlowTests.cpp`, `ChannelFlowTest` fixture):
       `CreateChannelsWrapsEveryChannellessTrackAsOneUndoStep` (two legacy tracks — a bare
       "Track Audio" and a "Track In" -> Oscillator, both wired straight to the output, the
       pre-P9-3 shape a real old project still loads as — both get their own strip sharing one
       `Master`, their old straight-to-output feeds are gone, and ONE undo/redo round-trips the
       whole sweep byte-for-byte against the graph/timeline/macro JSON snapshots taken right
       before the action), `CreateChannelsLeavesAlreadyChanneledTracksUntouched` (one track already
       channeled via `addAudioTrack()` plus one legacy track — the pre-existing strip's node and
       its wiring to Master are untouched; only the legacy track gets a new strip),
       `CreateChannelsIsANoOpWhenNothingNeedsAChannel` (every track already channeled — no new
       nodes, and undoing once removes the earlier `addAudioTrack()` step itself, proving "Create
       Channels" pushed no undo step of its own),
       `CreateChannelsGivesTwoTracksSharingOneInstrumentJustOneChannel` (D1, §7: two MIDI tracks
       wired into one shared channel-less Oscillator come out of the sweep with exactly one
       channel, not two, and one undo removes it).
   - **P9-3d — DONE (FRO25).** "Make channel" on a track or a selected chain — the last P9-3 flow;
     §5.8's "As implemented" note is the behaviour. Core: `synth::planMakeChannel` (a pure query:
     own/shared/side-input regions, exits, strip crossings, bus heads, `needsChannel`, `refusal`),
     `synth::buildMakeChannel` (the graph rebuild — the shared `buildChannelChain` gained a sink so
     a strip can feed a merge point's input pins instead of, or besides, Master's Mix — plus
     `addVoiceMixerForPolyInstrument` ahead of any poly feed) and `synth::resolveChannelSource`, all
     in `Source/Mixer/ChannelFlows.{h,cpp}`. AppUI: `GraphEditor::makeChannelFromNode` (boxes each
     channel via the group-time crossing plan minus the strip's own outlets — [`macros_implementation.md`](macros_implementation.md)
     §7 item 7 — and relocates Audio Output on a first Master, the T187 mirror),
     `duplicateIntoChannel`/`duplicateIntoChannelTargets`, the canvas and module-card menu items;
     `TimelineTrackHeaderComponent`'s "Make Channel" (`kMakeChannelMenuId`) over two new non-pure
     `TrackHeaderHost` virtuals (`canMakeChannelForTrack`/`makeChannelForTrack`);
     `MainComponent::makeChannelForNode`/`duplicateIntoChannel` wrap each action in ONE
     `recordGraphTimelineAndMacroChange` (refusal/no-op checked first, so neither pushes an undo
     step) and then run `reconcileTimelineAfterGraphChange()`; GraphEditor's own
     `onMakeChannelRequested`/`onDuplicateIntoChannelRequested` route its menu items there.
     - Tests (`Tests/ChannelFlowTests.cpp`): `ChannelFlowMakeChannelCore` —
       `ExclusiveChainAndItsOwnLfoMoveIntoTheChannelMacro`,
       `SharedLfoStaysOutsideThroughAnAutoPortAndTheRenderIsIdentical` and
       `MergePointBecomesItsOwnBusChannelAndTheRenderIsIdentical` (offline renders of the legacy and
       converted patch, two Hosted engines, compared sample for sample; the merge case then gives
       the second track its own channel into the same bus), `AlreadyChanneledOrGroupedTargetIsANoOp`;
       `ChannelFlowTest` — `TrackHeaderMakeChannelThroughTheRealRightClickIsOneUndoStep`,
       `CanvasSelectionMakeChannelThroughTheRealRightClick` (module card and empty-canvas
       right-clicks), `MergePointBecomesABusChannelInOneUndoStep`,
       `PolyChainGetsAVoiceMixerAheadOfTheStripInOneUndoStep`,
       `DuplicateIntoChannelGivesAnIndependentCopyAndLeavesTheOtherTrackOnTheOriginal` — every
       menu driven through its real right-click `mouseDown` and test hook, every action checked to
       undo in one step against graph/timeline/macro JSON snapshots and to redo.

3. **P9-4 (T177) — Track/channel link — DONE.** §5.2 states the behaviour; this is how it is built.
   - **The rule is a pure query, never a cached flag.** `synth::resolveTrackChannelLink`
     (`Source/Mixer/TrackChannelLink.h`) walks a track's bound node forward to the first
     `ChannelStripModule` it reaches, then that strip's feeders back out: exactly one feeder, and it
     is this track, **is** the link — so break and re-form need no bookkeeping. The backward walk is
     `StemSession`'s former private `upstreamTrackSources` promoted to Core (stem naming, §5.12,
     asks the same question), so `channelDisplayName` answers "which track names this" once for both.
   - **One interface, one collaborator.** `synth::ui::TrackChannelLinkSurface` is all a track header
     sees; `TrackChannelLinkController` implements it, owned by `MainComponent`, which gains only
     the member and a one-line `TrackHeaderHost::getChannelLinkSurface()` override (the
     `GraphCanvasHost` seam pattern). Every "not linked" answer is false/null, so the header falls
     through to its existing behaviour unchanged. Renaming the channel joins `renameMacro`'s own
     transaction through the new `MacroGroupController::recordMacroRenameHook` rather than becoming
     a second Cmd+Z; a linked M/S is one graph-snapshot step, solo always via §5.3's engine entry.
   - **Two decisions.** A channel's colour IS its macro's colour (no colour field on the strip), so
     an unboxed linked channel syncs neither name nor colour while its M/S still drive the strip.
     And soloing a linked channel silences every other channel, a shared one included, even when
     that shared channel's own track is note-gate soloed: a DAW mixer solo, not a bug.
   - **The channel chip** (`ChannelChipComponent`) appears on every header whose track reaches a
     channel, linked or shared. Its click calls `revealChannelForTrack`, today selecting the channel
     and panning it into view (the Locate Master contract) — **the P9-5 hook**: the mixer panel
     changes that one override, with no change to chip or header. ONE 15 Hz `juce::Timer` on
     `TimelinePanelComponent` ticks every header, never one per row; it idles while hidden, reads
     only `getChannelMeterPeak` (a cached strip id, not a graph walk), and each chip repaints only
     past a coarse threshold (`docs/layout_visuals_animation.md` §2-3).
   - Tests: `ChannelFlowTrackChannelLinkCoreTests.cpp` (the rule) and
     `ChannelFlowTrackChannelLinkTests.cpp` (the behaviour through the real header buttons,
     including the mixed linked-solo + shared-solo case asserted on the rendered strip meters).

4. **P9-5 (T174) — Mixer panel, tab beside the Timeline. DONE.** `MixerDockComponent`
   (`Source/UI/Mixer/`) hosts a two-tab strip (Timeline / Mixer, `kTabStripHeight = 22`px) above
   whichever panel is active, replacing `TimelinePanelComponent` as `MainComponent`'s direct bottom
   dock child; `MainComponent::isTimelineVisible`/the persisted `timelinePanelVisible` key now open
   and close the whole dock (either tab), and `MixerDockComponent`'s own `bottomDockActiveTab` key
   persists which tab is showing (default `"timeline"`) — see
   [`timeline_panel_core.md`](timeline_panel_core.md)'s "Docking, toggle, shortcut" FRO11 note for
   the full key-semantics change. `MixerPanelComponent` renders one `MixerColumnComponent` per
   strip in track order, then strips with no track (by node id), then Direct, then Master (§5.10);
   each column holds a `MixerFader` (linear-vertical slider bound 1:1 to the strip's gain param via
   `juce::SliderParameterAttachment`, dB readout, undo bracket via `parameterGestureChanged` ->
   `AppUndoManager::captureBeforeState`/`pushSnapshotFromCapture` so every fader/pan/M/S gesture is
   exactly one undo step) plus pan, M/S (routed through `AudioEngine::setChannelStripSoloed` for
   solo, never the module directly — §5.3), and a peak meter. Meters ride
   `MainComponent`'s existing 10 Hz timer tick (no new timer), gated on
   `mixerDock.isMixerTabActive() && mixerDock.isVisible()`. `Cmd+Alt+M` (`toggleMixerPanel`
   command/shortcut) opens the dock on the Mixer tab, or closes it on a second press when already
   open on Mixer (mirrors the existing Toggle Timeline button's open/close symmetry).
   - Tests: `Tests/UI/Mixer/` (`MixerDockComponentTests.cpp` — tab switching, the toggle command's
     open-on-Mixer/close-on-second-press behaviour, the command table round trip, active-tab
     persistence across an `ApplicationProperties` reload; `MixerPanelComponentTests.cpp` — one
     column per strip plus Direct plus Master, a themed PNG render smoke test in both built-in
     themes, clicking a column selects its owning macro on the canvas; `MixerFaderTests.cpp` — the
     slider/param binding and dB readout, including a regression test for a `parameterValueChanged`
     UAF this ticket's implementation fixed, see "Deviations" below) plus
     `Tests/Mixer/MixerModel/` for the headless column-ordering/query logic. Also updated: every
     `Tests/UI/Timeline/TimelinePanel/*` and `Tests/UI/Layout/PanelAnimationAndLoadingTests.cpp`
     case that asserted the panel's own bounds or visibility, since `TimelinePanelComponent` is now
     nested inside `MixerDockComponent` instead of being `MainComponent`'s direct child (both a
     different coordinate space and a different visibility-flag composition — see those files'
     own FRO11 comments).
   - Deviations found and fixed while implementing (not present before this ticket): (1) a
     `MixerFader::parameterValueChanged` listener callback captured a raw `this` in a
     `MessageManager::callAsync` lambda — a queued callback could fire after the fader was
     destroyed (graph rebuild, or `MixerPanelComponent::rebuild()`), a use-after-free; fixed with a
     `juce::Component::SafePointer`, this codebase's standing convention for this exact pattern. (2)
     `MainComponent::timerCallback()`'s 10 Hz poll gate still read the renested
     `timelinePanel.isVisible()` alone, which only reflects "the Timeline tab is selected" post-nesting,
     not "the dock is open" — the poll kept running while the whole dock was closed; fixed by
     composing `timelinePanel.isVisible() && mixerDock.isVisible()`. (3) the timeline panel's
     resize-drag wiring (`MainComponentSetupTimeline.cpp`) passed the panel's self-reported content
     height straight to `setTimelinePanelHeight()`, which owns the *total* dock-carve height —
     every real resize-drag left the panel 22px (the tab strip) shorter than the user dragged to;
     fixed by adding `MixerDockComponent::kTabStripHeight`, now public for exactly this seam. (4)
     "Own panel" and "Window" placement (§5.9) and the Mixer tab's own resize grab strip are not
     part of this ticket — the dock cannot be resized while the Mixer tab is active (the resize
     handle lives on `TimelinePanelComponent`); tracked under P9-6/follow-up, not a regression from
     before this ticket (there was no mixer panel to resize before it). (5) FRO11 follow-up (PR
     #374 CI): a graph-structural undo/redo (or New Patch/Load/AI patch apply) froze the affected
     `ChannelStripModule`/`MasterModule` nodes' parameters before the mixer's own `MixerFader`/pan
     `SliderParameterAttachment` had let go of them — `MixerPanelComponent::rebuild()` (reached from
     the AFTER-restore hook, `reconcileTimelineAfterGraphChange()`) destroyed the stale column
     *after* the restore had already freed what it pointed at, so `~MixerFader` -> `unbind()` ->
     `AudioProcessorParameter::removeListener()` ran on freed memory (a Linux CI hang:
     `ChannelFlowTest.CreateChannelsIsANoOpWhenNothingNeedsAChannel`, exit 124 + SIGABRT — a
     deadlock inside `CriticalSection::enter` on freed memory; macOS/Windows passed only by luck).
     Fixed the same way `ModuleComponent` already handles this class of bug
     (`GraphEditor::detachAllModuleComponents()`, called before every graph-replacing mutation):
     added `GraphEditor::onBeforeDetachAllModuleComponents`, fired at the top of
     `detachAllModuleComponents()` — the one seam every such call site already funnels through —
     wired by `MainComponent` to `MixerPanelComponent::unbindAllColumns()` (unbinds every strip
     column's + Master's fader/pan/mute/solo/meter and clears their raw pointers, without
     destroying anything; `MixerColumnComponent::unbindFromGraph()` /
     `MixerMasterColumn::unbindFromGraph()`, both idempotent/null-safe like `MixerFader::unbind()`
     itself). `~MainComponent()`'s own `graphEditor.detachAllModuleComponents()` call (already
     ordered before `audioEngine.shutdown()`) now covers the same teardown-ordering hazard for the
     mixer for free. Regression tests: `Tests/UI/Mixer/MixerPanelUndoUnbindTests.cpp`
     (`MixerPanelUnbindsBeforeAGraphRestoreSoUndoNeverTouchesFreedParameters`,
     `MixerPanelUnbindsBeforeNewPatchReplacesTheDocument`), asserting via
     `MixerFader::getLiveUnbindCallCountForTest()` that the hook actually ran, not just that
     nothing crashed.

5. **P9-6 (T175) — Detachable windows for Timeline + Mixer, and the placement preference.** One
   mechanism for both, icon-only detach control, keyboard focus scoped per window (T158).
   - Tests: a detached mixer window built with `addToDesktop=false` behaves identically to the
     docked panel in a headless test; keyboard focus in one detached window does not leak into the
     other; switching the placement preference moves the panel without losing its state.

6. **P9-7 (T176) — Track presets. DONE.** See §5.7 for the design as it landed
   (`TrackPresetManager`, the outside-modulator walk-and-copy rule, the solo scrub, the header/macro
   menu entry points, the per-type defaults consulted by `+ Track`).
   - Tests: `Tests/Mixer/TrackPreset/TrackPresetTests.cpp`,
     `TrackPresetCaptureTests.cpp`, `TrackPresetDefaultsTests.cpp` (see §5.7's "As implemented" for
     what each proves) plus four cases in `TimelineTrackHeaderContextMenuTests.cpp`.
   - Not yet covered by a test (manual verification only): the `+ Track` submenu listing every
     saved preset by type and "Insert Track Preset from File..." (`TimelinePanelTrackHeaders.cpp`),
     both implemented but exercised only by clicking through the app.

Side tracks (each independent of the main line beyond its own listed dependency):

- **P9-8 (T123) — Stem export. DONE.** See §5.12 for the design as it landed
  (`ChannelStripModule`'s stem tap, `StemExporter`/`StemSession`/`StemRunner`, the two decisions on
  muted/soloed strips and Master's Direct). `Tests/Engine/StemExportTests.cpp`: N strips produce N files
  of equal length; the written stems sum back to the pre-Master mix within a tolerance, proven
  against a non-unity Master gain; non-default strip gain/pan prove the tap is post-fader; a muted
  strip's file is silent and the sum property still holds; a soloed strip during export does not
  affect which strips get written and leaves solo/mute state unchanged; a cancelled or failed
  export leaves no stem files behind, never touches a pre-existing file, and disarms every tap.
- **P9-9 (T178) — Sends and group buses.** After P9-5; needs a short design pass of its own before
  implementation (a send is a tap on a strip feeding a bus channel, per §9, but the mechanism
  itself isn't specified here).
- **P9-10 (T179) — EQ curve thumbnail on mixer columns.** After P9-5. **DONE.** How it landed:
  - *Curve maths* — `synth::ui::EqResponseCurve::compute` (`Source/UI/Mixer/EqResponseCurve.h`), 48
    log-spaced points over +/-18 dB, built the same way `EQCurveComponent::recomputeMagnitudes()`
    does: snapshot the bands + output gain once, then sample `ParametricEQModule::responseDb`. No
    second approximation of the DSP — that header's own comment forbids one.
  - *Component* — `synth::ui::MixerEqThumbnail` (`Source/UI/Mixer/MixerEqThumbnail.{h,cpp}`), a
    `juce::Component` that registers as a `juce::AudioProcessorParameter::Listener` on every one of
    the bound EQ's parameters. `parameterValueChanged` (any thread — a CV-modulated bell writes its
    resolved value from the audio thread) only calls the allocation-free, coalescing
    `juce::AsyncUpdater`; the actual recompute + repaint happens once, on the message thread, in
    `handleAsyncUpdate()` — the same thread-hop `HostedPluginModule::InstanceListener` uses for its
    own `audioProcessorChanged`. `paint()` reads only the cached magnitudes/bypass flag, never the
    module, satisfying "no unconditional per-tick repaint" by construction (there is no Timer).
    Destruction (and every rebind) cancels the pending update and detaches every listener before
    the module pointer can go stale — the same ordering `MixerFader::unbind()` uses.
  - *Finding "the" EQ* — `MixerColumnComponent::setColumn()` walks `column.inserts` (already
    signal-ordered) and binds the first `ParametricEQModule` it finds; a second EQ further down the
    chain stays reachable through the insert list itself but gets no thumbnail of its own (Cubase's
    own single-slot idiom — a deliberate scope trim, not an oversight).
  - *Click* — forwarded through the column's EXISTING `onEditOnCanvas` seam with the EQ node's own
    uuid, so `MixerPanelComponent::selectOnCanvas` resolves it exactly like a column header click or
    the insert list's own "Edit on canvas" link (macro id, then `findByMember`, else the bare node).
    No new canvas plumbing.
  - *Lifetime* — two seams, not one. `MixerColumnComponent::unbindFromGraph()` (the FRO11
    pre-restore hook, `MixerPanelComponent::unbindAllColumns()` reached via
    `GraphEditor::onBeforeDetachAllModuleComponents`) detaches the thumbnail's listeners for every
    graph-*replacing* mutation (undo/redo restore, New Patch, Load, AI apply), same as it already
    does for the fader/pan/mute/solo/meter. A **live single-insert removal** from the mixer's own
    row menu is a different path — `MixerInsertList::removeRow()` calls `graph.removeNode()`
    directly (synchronous, frees the processor immediately) and only *afterwards* does that
    mutation's `onMutated` bubble into `MixerPanelComponent::rebuild()`, which is what would
    destroy this column's `eqThumbnail_` — too late to save it from a stale `eq_` pointer, and
    `onBeforeDetachAllModuleComponents` never fires for this path at all. Fixed by
    `MixerInsertList::onBeforeNodeRemoved` (fired from `removeRow()`, with the node about to be
    freed, before `graph.removeNode()`): `MixerColumnComponent` wires it to unbind
    `eqThumbnail_` whenever the node being removed is the one it's bound to. An earlier draft of
    this plan's own risk notes ("EQ node deleted between snapshot and paint: covered by the column
    rebuild lifetime") got this ordering backwards — the rebuild happens strictly *after* the
    module is freed on this path, not before; the notes below are the corrected version.
    Belt-and-braces: `MixerInsertList::removeRow()` is not the only single-node
    `graph.removeNode()` call site with no pre-removal unbind hook — a canvas "Delete" on the same
    EQ module's card (`GraphEditor::requestDeleteModule()`) is another. Rather than chase every
    such call site with its own hook, `setEqModule()` also takes the owning graph + NodeID
    (`MixerColumnComponent` always has both), and `detachListeners()` checks the node is still
    actually in the graph before touching `eq_` at all — if it's already gone, its parameters died
    with it and there is nothing left to call `removeListener()` on.
  - Tests: `Tests/UI/Mixer/MixerEqThumbnailTests.cpp` — hidden with no EQ; visible with an enabled
    band; dark- and light-theme PNG renders of a flat vs. a +12 dB/1 kHz curve are not pixel-
    identical; bypass visibly dims the fill; recompute count stays flat across repeated paints and
    bumps by exactly one per parameter-change-plus-dispatch-pump; a synthesized click fires
    `onClicked`. `Tests/UI/Mixer/MixerColumnComponentTests.cpp` — a column's click forwards the EQ's
    own uuid (not the strip's); with two EQ inserts only the first gets the thumbnail; a strip with
    no EQ insert shows none; removing the currently-bound EQ insert through
    `MixerInsertList::removeRow()` (a real, signal-connected TrackAudio->EQ->Compressor->Strip
    chain, so `spliceOutInsert` actually runs) unbinds the thumbnail before the node is freed,
    proven by `MixerEqThumbnail::getLiveUnbindCallCountForTest()` (same accounting as
    `MixerFader::getLiveUnbindCallCountForTest()`) rather than by the absence of a crash alone.
    `MixerEqThumbnailTests.cpp`'s own
    `DoesNotTouchFreedParametersWhenTheNodeWasRemovedWithoutUnbindingFirst` covers the
    belt-and-braces liveness check directly — a node removed from the graph with NO unbind call at
    all (the canvas-delete shape) must still not touch the freed module.
- **P9-11 (T180) — Gate module.** Done — `GateModule` (`Source/Modules/FX/GateModule.h`,
  [`fx_modules.md` § Gate Module](fx_modules.md#gate-module)). No dependency on the rest of P9;
  wiring it into a default track preset (§5.7/§7 D3) is still open.
- **T181 — Mixer accessibility**, in the Accessibility epic: column navigation, the existing
  rebindable M/S keys acting on the focused column, fader nudge, screen-reader labels for faders
  and meters, alongside T158's app-wide keyboard focus work.

