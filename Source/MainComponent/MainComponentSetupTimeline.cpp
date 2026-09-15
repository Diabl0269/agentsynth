// MainComponentSetupTimeline.cpp -- initialiseCommon()'s timeline-panel wiring step
// (wireTimelinePanel), split by concern into services/shortcuts, the 4-hook TimelineDoc
// inventory, clip-lane callbacks, and the transport-bar record toggle. Split out of the former
// single MainComponent.cpp (FRO76) -- see MainComponent::initialiseCommon in MainComponent.cpp.
#include "MainComponent.h"
#include "MainComponentInternal.h"
#include <algorithm>

void MainComponent::wireTimelinePanel() {
    wireTimelinePanelServicesAndShortcuts();
    wireTimelineHookInventory();
    wireTimelineClipLaneCallbacks();
    wireTimelineRecordToggle();
}

void MainComponent::wireTimelinePanelServicesAndShortcuts() {
    addAndMakeVisible(toggleAiPanelButton);
    toggleAiPanelButton.setComponentID("toggleAiPanel");
    toggleAiPanelButton.onClick = [this] {
        isAiPanelVisible = !isAiPanelVisible;
        // Persist BEFORE the slide so a crash during layout doesn't lose the user's choice.
        appProperties.getUserSettings()->setValue("aiPanelVisible", isAiPanelVisible ? "1" : "0");
        appProperties.getUserSettings()->saveIfNeeded();
        applyToolbarIcons();
        // Everything geometric — showing/hiding the panel, the slide, the synchronous landing when
        // there is no VBlank to slide on — belongs to the one shared seam.
        beginPanelSlide();
    };

    // Bottom-docked timeline panel toggle. Mirrors the AI-panel handler above exactly (flip +
    // persist BEFORE the slide, applyToolbarIcons, then beginPanelSlide) — the axis it slides on
    // is resized()'s business, not the toggle's.
    // Wire the panel to the real transport + persisted settings.
    timelinePanel.setTransport(&audioEngine.getTransport());
    timelinePanel.setMetronome(&audioEngine.getMetronome());
    timelinePanel.setApplicationProperties(&appProperties);

    // FRO11 (P9-5): the dock's own persisted-tab key, read once and written on every tab click
    // (docs/layout.md's "Panel collapse and persistence" table); the graph-topology-mutated and
    // Direct's "Make channel" callbacks route the mixer's own actions through the SAME funnels
    // every other "Make channel" trigger and every other graph-structural edit already use.
    mixerDock.setApplicationProperties(&appProperties);
    mixerDock.setOnGraphTopologyChanged([this] { reconcileTimelineAfterGraphChange(); });
    mixerDock.setOnMakeChannelForNode([this](juce::AudioProcessorGraph::NodeID source) { makeChannelForNode(source); });
    // FRO12 (P9-6): each panel's ONE detached-window focus region (T159/docs/shortcuts.md) --
    // stored on the host now, applied to whichever DetachedPanelWindow it builds later. Re-running
    // MainComponent's own registration pass on every detach/redock (rather than reordering/renaming
    // anything already registered above) is the guard rule the plan's focus section spells out.
    mixerDock.getTimelineHost().setHostedPanelFocusRegion("timeline", timelinePanel);
    mixerDock.getMixerHost().setHostedPanelFocusRegion("mixer", mixerDock.getMixerPanel());
    mixerDock.onPanelDetachStateChanged = [this] { rebuildFocusRegions(); };
    // Placement preference (Tab/Own panel/Window, docs/mixer.md §5.9) -- read once here (both
    // panels already exist by this point in initialiseCommon()'s ORDER) and again on every
    // settings-file write, see MainComponent::changeListenerCallback's settings branch.
    mixerPlacement_.applyPlacementPreference();
    // FRO11 crash fix: the mixer's fader/pan bindings are raw pointers into live processor
    // parameters, exactly like ModuleComponent's own -- so they unbind through the SAME seam
    // ModuleComponent already uses (GraphEditor::onBeforeDetachAllModuleComponents, fired at the
    // top of detachAllModuleComponents() -- every graph-replacing path funnels through it,
    // undo/redo's lazy preRestore included). Without this, a graph-structural undo/redo that frees
    // a ChannelStripModule while the mixer still held its gain param destroys the stale
    // MixerColumnComponent from mixerDock.rebuildMixer() (reconcileTimelineAfterGraphChange(),
    // called from the AFTER-restore hook) AFTER the restore already freed the param --
    // MixerFader::unbind()'s removeListener() on that freed memory is what hung the Linux CI build
    // (deadlock inside CriticalSection::enter on freed memory) that this fixes.
    graphEditor.onBeforeDetachAllModuleComponents = [this] { mixerDock.getMixerPanel().unbindAllColumns(); };
    // The channel chip's click (TrackChannelLinkSurface::revealChannelForTrack, "THE P9-5 HOOK"
    // per its own comment): open the dock (same sequence performToggleMixerPanel's own "closed"
    // branch runs) before revealColumnForStrip switches tabs and scrolls to the column -- a closed
    // dock has nothing on screen to scroll to yet.
    trackChannelLink_.setMixerRevealHook([this](juce::AudioProcessorGraph::NodeID stripId) {
        if (!isTimelineVisible) {
            isTimelineVisible = true;
            appProperties.getUserSettings()->setValue("timelinePanelVisible", "1");
            appProperties.getUserSettings()->saveIfNeeded();
            applyToolbarIcons();
            beginPanelSlide();
        }
        return mixerDock.revealColumnForStrip(stripId);
    });

    // The user's bindings for the three surfaces that resolve their OWN keys (see
    // PianoRollComponent::setShortcutManager for the strict-resolution contract). All three are
    // installed together and MUST stay together: with a manager installed, resolution is strict —
    // an action id missing from ShortcutManager::resetToDefaults() has NO key at all rather than
    // falling back to its hardcoded default, so installing the manager before registering an id
    // makes that key silently inert. ShortcutManagerTest's surface-id tripwire pins the id list
    // these three consult against the defaults table for exactly that reason.
    timelinePanel.setShortcutManager(&shortcutManager);
    timelinePanel.getPianoRoll().setShortcutManager(&shortcutManager);
    timelinePanel.getClipLaneArea().setShortcutManager(&shortcutManager);
    // FRO18: the mixer panel resolves the SAME "timelineMuteFocusedTrack"/"timelineSoloFocusedTrack"/
    // "timelineArmFocusedTrack" action ids the track-header row above already binds -- a user's
    // rebind of M/S/R applies to whichever of the two surfaces has focus. Not part of the "MUST
    // stay together" strict-resolution group above: the mixer panel falls back to hardcoded bare
    // letters with no manager installed (MixerPanelComponent::matchesAction), same "no manager
    // installed" contract every other surface action in this app follows, rather than requiring
    // every id it consults to be pre-registered.
    mixerDock.getMixerPanel().setShortcutManager(&shortcutManager);
    // FRO18: Arm reaches the focused strip's linked track through the SAME performTrackEdit
    // one-undo-step path the Timeline header row's own R key uses -- never a direct TimelineDoc
    // write (that would skip the undo bracket every other track edit goes through). Routed through
    // setOnArmTrack, the sibling forwarder to setOnGraphTopologyChanged/setOnMakeChannelForNode
    // above, rather than reaching through getMixerPanel() to set the panel's callback directly.
    mixerDock.setOnArmTrack([this](synth::TrackId id) {
        performTrackEdit([this, id] {
            if (auto* track = timelineDoc.getTrack(id))
                timelineDoc.setTrackArmed(id, !track->armed);
        });
    });

    // The panel's top-edge drag reports a desired height; THIS component owns it — clamp, lay out
    // live, and persist once the drag ends (not per pixel).
    //
    // FRO11 (P9-5): the panel reports its own desired CONTENT height (TimelinePanelComponent::
    // ResizeHandle::desiredHeightFor stays agnostic of whatever chrome it sits inside), but
    // setTimelinePanelHeight owns the TOTAL dock-carve height -- mixerDock's own tab strip above
    // that content, whenever the timeline is showing inside the shared dock rather than
    // standalone. This is the one seam that knows about both, so it adds the difference.
    timelinePanel.onResizeHeight = [this](int desiredHeight) {
        setTimelinePanelHeight(desiredHeight + synth::ui::MixerDockComponent::kTabStripHeight, /*persist=*/false);
    };
    timelinePanel.onResizeHeightCommitted = [this](int desiredHeight) {
        setTimelinePanelHeight(desiredHeight + synth::ui::MixerDockComponent::kTabStripHeight, /*persist=*/true);
    };
}

void MainComponent::wireTimelineHookInventory() {
    // This component owns the app's one live TimelineDoc, so it owns the four hooks that
    // keep the rest of the system in step with it. The full inventory is in docs/architecture.md
    // ("App wiring") — keep the two in sync.
    //
    //  1. PUBLISH-ON-CHANGE: every effective doc mutation notifies us, and we republish the
    //     snapshot to the audio thread and rebuild the recorder's lane bindings.
    //  2. RECORDER: attached to (doc, undo, transport) and registered with the engine so the
    //     applier can see its gesture claims; driven by update() on the existing 10 Hz timer.
    //  3. RESTORE HOOKS: undo/redo suspends capture for the span of the restore and reconciles
    //     bindings afterwards (both domains — see AppUndoManager::setRestoreHooks).
    //  4. PANEL: the track-header column reads the doc and calls back into us (TrackHeaderHost)
    //     to create, re-bind and delete the Track In nodes its chips name.
    timelineDoc.addListener(this);
    automationRecorder.attachTo(timelineDoc, undoManager, audioEngine.getTransport());
    audioEngine.setAutomationRecorder(&automationRecorder);
    undoManager.setRestoreHooks(
        [this] { programmaticApplyScopes.push_back(std::make_unique<ProgrammaticApplyScope>(*this)); },
        [this] {
            if (!programmaticApplyScopes.empty())
                programmaticApplyScopes.pop_back();
            reconcileTimelineAfterGraphChange();
        });
    timelinePanel.setTrackHeaderHost(this);
    timelinePanel.setTimelineDoc(&timelineDoc);
    // Same undo stack every graph/timeline mutation already shares — a clip drag/trim/
    // split/duplicate/delete is one more AppUndoManager::recordTimelineChange call, same as every
    // other timeline-only edit.
    timelinePanel.setUndoManager(&undoManager);
}

void MainComponent::wireTimelineClipLaneCallbacks() {
    // The SAME resolution the audio streamer uses (AudioClipStreamer::resolveAssetRef),
    // re-targeted at the peaks sidecar via peaksRefForAssetRef() — see that function's comment.
    // One shared resolver: the clip-lane area never re-derives bundle-vs-Recordings root logic.
    timelinePanel.getClipLaneArea().setPeaksResolver([this](const juce::String& assetRef) -> juce::File {
        return audioEngine.getAudioClipStreamer().resolveAssetRef(detail::peaksRefForAssetRef(assetRef));
    });
    // Same resolution playback uses, answering existence rather than handing back a File —
    // what paints the missing-asset placeholder instead of an (impossible) waveform.
    timelinePanel.getClipLaneArea().setAssetExistsResolver([this](const juce::String& assetRef) -> bool {
        return audioEngine.getAudioClipStreamer().resolveAssetRef(assetRef) != juce::File();
    });
    // "Relink audio…" bubbles up here rather than being handled inside the lane area itself
    // — it needs a host FileChooser and synth::AssetManager import, neither of which that class has.
    timelinePanel.getClipLaneArea().onRelinkAudioRequested = [this](synth::ClipId id) { promptRelinkClipAsset(id); };
    // Same division of labour for the authoring gestures: the lane area decides WHICH audio track
    // and WHICH beat (double-click on an empty audio row, or an OS file drop on one), and this owns
    // the import + clip creation, because only it knows the bundle root.
    timelinePanel.getClipLaneArea().onAudioFileDropped = [this](synth::TrackId track, double startBeat,
                                                                juce::File file) {
        importAudioFileToClip(track, startBeat, file);
    };
    // P on the clip lanes = loop the selection. The lane area knows the span; only this owns the
    // transport (same division as every other callback above). Whether P also ARMS looping is the
    // "timelineLoopSelectionArms" preference (default yes; off = place the locators, keep the
    // current loop state) — the same key TimelinePanelComponent's own P fallback reads.
    timelinePanel.getClipLaneArea().onLoopRangeRequested = [this](double startBeat, double endBeat) {
        auto& transport = audioEngine.getTransport();
        bool arm = true;
        if (auto* settings = appProperties.getUserSettings())
            arm = settings->getBoolValue("timelineLoopSelectionArms", true);
        transport.setLoop(startBeat, endBeat, arm || transport.getPositionSnapshot().looping);
    };
    // BEFORE the first publish below — publishTimeline() syncs the clip streamer, and it can
    // only resolve an asset ref once it knows the roots.
    refreshAssetRoots();
    // One publish before anything else happens, so the audio thread starts from this document
    // rather than from the exchange's never-published empty fallback.
    publishTimelineAndRebindRecorder();
}

void MainComponent::wireTimelineRecordToggle() {
    // MidiRecorder is now app-wired (docs/architecture.md's hook inventory gains a
    // fifth entry) — this component owns the one live MidiRecorder, since it is the only thing
    // that can see both the armed tracks (timelineDoc) and the transport bar's record button.
    audioEngine.setMidiCaptureSink(&midiRecorder);
    timelinePanel.getTransportBar().onRecordToggled = [this](bool wantRecording) { handleRecordToggle(wantRecording); };
}

// The transport bar's onRecordToggled body, extracted out of wireTimelineRecordToggle() (FRO76).
void MainComponent::handleRecordToggle(bool wantRecording) {
    if (!wantRecording) {
        // Both are no-ops unless their own kind of take is in flight, so Record-off can call
        // them unconditionally and neither path has to know the other exists.
        commitAudioRecording();
        commitMidiRecording();
        return;
    }

    // Record does NOT require an armed track (see docs/timeline_panel_core.md's transport
    // section) — pressing Record always rolls the transport with the record indicator lit,
    // capturing on whichever track (if any) happens to be armed. With nothing armed this is
    // identical to Play plus a lit record indicator, plus a transient status-bar notice so the
    // silence isn't mistaken for a bug.
    //
    // The lookup considers Audio tracks too, and FIRST-ARMED WINS. With one armed track
    // of each kind the one earlier in the document decides which kind of take this is; there is
    // deliberately no "record both at once" (two takes, two commits, two undo steps for one
    // gesture). Automation-kind tracks are not recordable and are skipped.
    synth::TrackId armedTrack;
    synth::TrackKind armedKind = synth::TrackKind::Midi;
    for (const auto& track : timelineDoc.getTracks()) {
        if (track.armed && (track.kind == synth::TrackKind::Midi || track.kind == synth::TrackKind::Audio)) {
            armedTrack = track.id;
            armedKind = track.kind;
            break;
        }
    }
    const bool anyArmed = armedTrack.isValid();

    // An audio take's tap and its destination files are resolved BEFORE the transport
    // moves, for the same reason as always — a request that cannot be honoured must not leave
    // the transport rolling. Only reachable with an armed Audio track; with nothing armed (or a
    // MIDI track armed) there is no take to resolve here.
    const bool isAudioTake = anyArmed && (armedKind == synth::TrackKind::Audio);
    AudioTake take;
    RecordTapModule* tapModule = nullptr;
    if (isAudioTake) {
        auto* tapNode = ensureMasterRecordTap();
        tapModule = tapNode != nullptr ? dynamic_cast<RecordTapModule*>(tapNode->getProcessor()) : nullptr;
        if (tapModule == nullptr) {
            timelinePanel.getTransportBar().setRecordingState(false);
            statusBar.showMessage("Can't record audio: no Audio Output in the patch");
            return;
        }
        take.track = armedTrack;
        take.tapNode = tapNode->nodeID;
        if (!chooseTakeFiles(take)) {
            timelinePanel.getTransportBar().setRecordingState(false);
            statusBar.showMessage("Can't record audio: could not create the take file");
            return;
        }
    }

    // Record implies roll (DAW convention): starting a take also starts the transport if it
    // isn't already running.
    auto& transport = audioEngine.getTransport();
    const auto snap = transport.getPositionSnapshot();
    const int countInBars = timelinePanel.getTransportBar().getCountInBars();

    // Count-in pre-roll, only from a full stop — a record engaged while already playing
    // gets no pre-roll (the user is already mid-performance) and no forced click, matching
    // today's plain "record implies roll" behaviour exactly.
    double punchInBeat = snap.ppq;
    if (!snap.playing && countInBars > 0) {
        const double beatsPerBar = (double)snap.timeSigNumerator * 4.0 / (double)std::max(1, snap.timeSigDenominator);
        const double preRollStart = std::max(0.0, punchInBeat - (double)countInBars * beatsPerBar);
        transport.locateBeat(preRollStart);
        // Forced audible through the pre-roll regardless of the user's own metronome toggle;
        // cleared by the 10 Hz poll once the transport reaches punchInBeat (see timerCallback)
        // and unconditionally by commitMidiRecording() on stop.
        audioEngine.getMetronome().setForcedOn(true);
        transport.play();
    } else {
        if (!snap.playing)
            transport.play();
        punchInBeat = transport.getPositionSnapshot().ppq;
    }

    if (isAudioTake) {
        // The capture starts HERE, at record-on — not on the 10 Hz poll, which is what
        // used to cost a take up to ~100 ms of head. The count-in's pre-roll is therefore
        // RECORDED, and excluded from the committed clip by a trim (see commitAudioRecording);
        // the tap itself reports the exact transport sample its frame 0 landed on, so the clip's
        // placement is sample arithmetic rather than a poll observation.
        //
        // Deliberately AFTER the locate/play posted above, not before: those are transport
        // commands that take effect at the top of a block, and a frame captured before a locate
        // belongs to the OLD position — which would break the one thing the anchor promises,
        // that take frame `f` sits at `captureStart + f`. Starting after them can cost at most
        // the one block that may already be in flight (~10 ms at 512/48k), and that block is
        // pre-roll, honestly accounted for either way.
        const double rate = snap.sampleRate > 0.0 ? snap.sampleRate : 44100.0;
        if (!tapModule->startCapture(take.wavFile, take.peaksFile, rate, RecordTapModule::kNumChannels)) {
            // The tap vanished, or the file could not be opened. Nothing to salvage.
            timelinePanel.getTransportBar().setRecordingState(false);
            audioEngine.getMetronome().setForcedOn(false);
            statusBar.showMessage("Can't record audio: the take file could not be opened");
            return;
        }
        take.punchInBeat = punchInBeat;
        take.capturing = true;
        // Frozen NOW, not re-read at commit time — see the AudioTake field comments.
        take.captureSampleRate = rate;
        take.captureBpm = snap.bpm > 0.0 ? snap.bpm : 120.0;
        take.captureRecordingLatencySamples = audioEngine.getRecordingLatencySamples();
        audioTake_ = take;
    } else if (anyArmed) {
        // punchInBeat is BOTH the recorder's own bookkeeping and the audio-thread filter
        // threshold — captureBlock() drops everything before it, so the pre-roll bars the
        // performer plays along with the click are heard but never committed.
        midiRecorder.startRecording(armedTrack, punchInBeat);
    } else {
        // Nothing armed: the transport is rolling and the indicator is about to light, but no
        // take of either kind starts. Arming a track MID-ROLL does not retroactively start one
        // either — TimelineDoc::setTrackArmed() has no listener watching for this; the user has
        // to stop and press Record again once something is armed.
        statusBar.showMessage("Recording started - no track is armed");
    }

    // Lit regardless of arming — a bare "record" is still record-on.
    timelinePanel.getTransportBar().setRecordingState(true);
}
