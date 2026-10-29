// TimelinePanelComponent.cpp
//
// TimelinePanelComponent's constructor/destructor, shortcut-manager wiring
// (setShortcutManager/refreshShortcutTooltips) and the transport/doc/undo-manager setters
// (setTransport/setMetronome/updateFromTransport/setTimelineDoc/setUndoManager) -- the
// class's core lifecycle and wiring. TimelinePanelComponent is declared in
// TimelinePanelComponent.h; sibling TimelinePanel*.cpp files in this directory hold the
// rest of the class (edit-tool strip, automation lanes, clip clipboard, shortcuts, track
// headers, layout/paint).

#include "TimelinePanelComponent.h"
#include "ShortcutManager/ShortcutManager.h"
#include "Timeline/SongKeyNotes.h"
#include "Transport/TransportService.h"
#include "UI/DrumMachine/DrumMachineHost.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
// The edit-tool strip in the transport bar: square icon buttons in their own radio group.
constexpr int kEditToolRadioGroupId = 4300;

// A tool's button; Draw's is the pen (DrawPenButton), reported through `pen`.
std::unique_ptr<juce::DrawableButton> makeToolButton(EditTool tool, DrawPenButton*& pen) {
    const auto title = juce::String(editToolName(tool)) + " Tool";
    if (tool != EditTool::Draw)
        return std::make_unique<juce::DrawableButton>(title, juce::DrawableButton::ImageOnButtonBackground);
    auto button = std::make_unique<DrawPenButton>(title);
    pen = button.get();
    return button;
}
} // namespace

//==============================================================================
// P in the roll loops the selected notes. The roll only decides the span; the clip lane's callback is
// the one owner of transport.setLoop (and the arm-looping preference).
void TimelinePanelComponent::wirePianoRollLoopRequest() {
    pianoRoll_.onLoopRangeRequested = [this](double startBeat, double endBeat) {
        if (clipLaneArea_.onLoopRangeRequested)
            clipLaneArea_.onLoopRangeRequested(startBeat, endBeat);
    };
}

TimelinePanelComponent::TimelinePanelComponent() {
    // Makes grabKeyboardFocus() on THIS component (the "timeline" focus region's root)
    // succeed deterministically, rather than depending on JUCE's position-ordered descent into
    // children finding a focus-wanting one. This is deliberately separate from the clip-lane-area
    // and piano-roll's OWN grabKeyboardFocus() calls on mouseDown (docs/control/shortcuts.md's edit-surface
    // routing) — those still work exactly as before; Cmd+Shift+T / Tab just land on the panel root
    // itself rather than wherever positional descent happened to end up.
    setWantsKeyboardFocus(true);

    addAndMakeVisible(ruler_);

    addAndMakeVisible(addTrackButton_);
    addTrackButton_.setComponentID("timelineAddTrackButton");
    addTrackButton_.setIcon(synth::theme::Icon::ToolbarAddTrack, ToolbarGroup::Edit);
    // Tooltip text (it names the shortcut) is set by refreshShortcutTooltips() below.
    addTrackButton_.onClick = [this] { openAddTrackMenu(); };

    addAndMakeVisible(trackHeaderViewport_);
    trackHeaderViewport_.setComponentID("timelineTrackHeaderViewport");
    trackHeaderViewport_.setScrollBarsShown(true, false);
    trackHeaderViewport_.setViewedComponent(&trackHeaderList_, false);
    // A scrollbar drag on the header column moves the SHARED vertical scroll, so the lanes follow
    // it exactly like they follow the wheel. syncTrackScroll()'s own setViewPosition re-enters
    // here with an unchanged value and stops — no feedback loop.
    trackHeaderViewport_.onScrolledY = [this](int y) {
        if ((int)std::llround(viewState_.trackScrollY) == y)
            return;
        viewState_.trackScrollY = (double)y;
        clipLaneArea_.repaint();
        placeLaneBodies();
        repaint(gridLanesBounds_);
    };

    // Added before the snap combo so it sits left of it in z-order too (they never overlap,
    // but this keeps tab-order/z-order matching visual left-to-right order).
    addAndMakeVisible(transportBar_);
    transportBar_.setComponentID("timelineTransportBar");

    initSnapCombo();
    initSidePane();

    // The edit-tool strip, left of the snap controls in the transport bar (see resized()). Radio
    // buttons rather than a combo: which tool is active has to be readable at a glance mid-edit,
    // and the seven glyphs are the row every DAW user already knows.
    for (auto tool : kAllEditTools) {
        auto button = makeToolButton(tool, penButton_);
        button->setComponentID("timelineTool" + juce::String(editToolName(tool)));
        // Tooltip text (the CURRENT binding, not this hardcoded digit) is set by
        // refreshShortcutTooltips() below, once every tool button exists.
        button->setClickingTogglesState(true);
        button->setRadioGroupId(kEditToolRadioGroupId);
        // A tool button must never become the focused component. juce::Button's constructor opts
        // INTO keyboard focus, and a click grabs it by default, so picking a tool would otherwise
        // move focus out of the clip lane / piano roll — and
        // MainComponent::resolveEditSurface() reads real focus, so the very next Cmd+X would be
        // routed to the graph rather than to the clips the tool was just chosen for. Both calls
        // are needed: the second is what stops the CLICK grabbing focus, the first is what keeps
        // the button out of the tab chain.
        button->setWantsKeyboardFocus(false);
        button->setMouseClickGrabsKeyboardFocus(false);
        button->onClick = [this, tool] { setActiveTool(tool); };
        addAndMakeVisible(*button);
        toolButtons_[(std::size_t)tool] = std::move(button);
    }
    // Select is the default, and the strip must say so from the first frame.
    toolButtons_[(std::size_t)EditTool::Select]->setToggleState(true, juce::dontSendNotification);
    applyToolStripTheme();
    initDrawShapes();

    // The Sizing field sits right of the tools: it says what the Select tool's edge drags do to the notes.
    addAndMakeVisible(clipSizingButton_);
    clipSizingButton_.onModeChanged = [this](synth::ClipSizingMode mode) { clipLaneArea_.setClipSizingMode(mode); };

    // The snap toggle lives in the transport bar so grid magnetism is discoverable without opening
    // a clip — the same switch the piano roll's header "Q" and the panel-wide Q key flip.
    addAndMakeVisible(snapToggleButton_);
    snapToggleButton_.setComponentID("timelineSnapToggle");
    snapToggleButton_.setIcon(synth::theme::Icon::ToolbarSnap, ToolbarGroup::Edit);
    setToolbarLabelsShown(true);
    // Tooltip text is set by refreshShortcutTooltips() below.
    snapToggleButton_.setClickingTogglesState(false); // the shared view state is the truth
    snapToggleButton_.setToggleState(viewState_.snapEnabled, juce::dontSendNotification);
    snapToggleButton_.onClick = [this] { setSnapEnabled(!viewState_.snapEnabled); };

    // Follow-playhead sits next to the snap toggle and only mirrors follow_.enabled; its tooltip is
    // set by refreshShortcutTooltips() below.
    addAndMakeVisible(followPlayheadButton_);
    followPlayheadButton_.setComponentID("timelineFollowPlayheadToggle");
    followPlayheadButton_.setClickingTogglesState(false);
    followPlayheadButton_.setToggleState(follow_.enabled, juce::dontSendNotification);
    followPlayheadButton_.onClick = [this] { setFollowPlayheadEnabled(!follow_.enabled); };
    wireFollowPlayheadModes();

    // Added after everything else but BEFORE the playhead below, so clips draw above the
    // grid (painted by this component's own paint(), which — as a parent — always paints before
    // its children) and below the playhead.
    addAndMakeVisible(clipLaneArea_);
    wireClipLaneCallbacks();
    // Edge-scroll during a clip drag moves the SHARED view state; the ruler has no other way to
    // learn its beats moved (it isn't a drag participant), so this is the one pair-of-repaints
    // seam every other viewState_ scroll/zoom writer in this class already uses.
    clipLaneArea_.onViewScrolledByDrag = [this] {
        ruler_.repaint();
        repaint();
    };

    // Same slot as clipLaneArea_ (added right after it, before the playhead), but starts
    // invisible — addChildComponent (not addAndMakeVisible) keeps it hidden until openPianoRoll()
    // shows it.
    addChildComponent(pianoRoll_);
    wireDrumMachine();
    addChildComponent(rollLeaving_); // above the roll, below the playhead overlay added last
    pianoRoll_.setComponentID("timelinePianoRoll");
    pianoRoll_.onAboutToClose = [this] { rollLeaving_.capture(pianoRoll_); };
    pianoRoll_.onCloseRequested = [this] { closePianoRoll(); };
    // While the roll is open the ruler mirrors the roll's own mapping (installed in
    // openPianoRoll()), so every roll zoom/scroll must repaint it. The scale-assist panel opening/
    // closing is ALSO a mapping change (it moves leftGutterWidth()), so the override's x-offset has
    // to be re-issued too, not just repainted — otherwise the ruler keeps the offset it had when
    // the roll first opened and drifts from the grid the moment the panel toggles. Guarded on
    // isOpen(): closePianoRoll() clears the override itself and this must never re-install it
    // behind that call.
    pianoRoll_.onHorizontalViewChanged = [this] {
        if (pianoRoll_.isOpen())
            ruler_.setMappingOverride(&pianoRoll_.getRollViewState(), pianoRoll_.leftGutterWidth());
        ruler_.repaint();
    };
    wirePianoRollLoopRequest();
    // The roll's J key also says what it did, like the panel's own J.
    pianoRoll_.onSnapKeyToggled = [this](bool on) {
        if (onShowMessage)
            onShowMessage(on ? "Snap on" : "Snap off");
    };
    // The roll's Q button / Q key flipped the shared snapEnabled: persist it, sync the transport
    // bar's own Q toggle, and repaint the lanes+ruler that also paint the (now present/absent)
    // snap grid.
    pianoRoll_.onSnapToggled = [this] {
        persistSnapChoice();
        snapToggleButton_.setToggleState(viewState_.snapEnabled, juce::dontSendNotification);
        ruler_.repaint();
        repaint();
    };
    // NOTE AUDITION. The roll emits a pitch + on/off edge and knows nothing about the graph; the
    // only thing this panel adds is WHICH TRACK — resolved from the edited clip, since the roll's
    // own callback deliberately carries no clip/track (see PianoRollComponent::onAuditionNote) — and
    // then it is the host's job (MainComponent) to reach the track's bound Track In node.
    //
    // ASYMMETRIC BY DESIGN, and this is the whole correctness argument. A note-ON is disposable: no
    // host, no doc, roll closed or an unresolvable track all mean silence, which is fine. A note-OFF
    // is NOT — an audition note is deliberately exempt from every positional flush in
    // TimelineMidiSourceModule, so a dropped off hangs the note until the node is bypassed. So the
    // track resolved for the ON is LATCHED, and the matching OFF is routed to that latched track
    // unconditionally (host/doc null aside). It must not re-resolve, because by the time the off
    // fires the clip may be deleted, the roll closed, or a DIFFERENT clip open — all three of which
    // would either drop the off or, worse, send it to the wrong track.
    //
    // The latch holds at most one note (the roll sounds one note at a time and always emits its own
    // off before a retrigger's on — see startAudition), so a plain member is enough; it is cleared on
    // the off.
    pianoRoll_.onAuditionNote = [this](int pitch, float velocity01, bool on) {
        auditionClipNote(pianoRoll_.isOpen() ? pianoRoll_.getClipId() : synth::ClipId{}, pitch, velocity01, on);
    };

    initSongKeyField();

    // The automation lane editors, over the clip lanes and under the playhead.
    initAutomationLanes();

    // No transport-bar chrome may steal keyboard focus on click:
    // MainComponent::resolveEditSurface() reads REAL focus, so clicking the snap toggle (or any
    // other chrome control) would otherwise silently reroute the very next Cmd+X/C/V/D from the
    // clips to the graph — the same failure the edit-tool strip above opts out of. juce::Button
    // and a non-editable juce::ComboBox both enable click-grabs-focus in their constructors, so
    // this is an explicit opt-out. Focusability itself is left alone: a combo tabbed to on
    // purpose still takes focus; only the incidental mouse-click grab is disabled.
    for (juce::Component* chrome :
         {static_cast<juce::Component*>(&addTrackButton_), static_cast<juce::Component*>(&snapToggleButton_),
          static_cast<juce::Component*>(&followPlayheadButton_), static_cast<juce::Component*>(&snapCombo_),
          static_cast<juce::Component*>(&clipSizingButton_)})
        chrome->setMouseClickGrabsKeyboardFocus(false);

    // Added LAST so it is topmost — it draws over the ruler, the lanes grid AND the clips.
    addAndMakeVisible(playhead_);
    playhead_.setComponentID("timelinePlayhead");
    // The piano roll maps beats to x through its OWN zoom/scroll, so while it is open the overlay
    // hands it the drawn beat and leaves its rows alone entirely — one timer, two mappings. The
    // region itself is set in resized(), which is the only place the offset is known.
    playhead_.setLocalPlayheadClient(&pianoRoll_);

    initTrackHeaderWidthHandle(); // after the playhead: the seam handle must win the hit test

    // Every tool button, snapToggleButton_ and followPlayheadButton_ now exist — set their initial
    // (no-manager-installed, hardcoded-default) tooltip text. setShortcutManager re-runs this once
    // a real manager is wired, and again on every bindings-changed notification.
    refreshShortcutTooltips();
}

// LIFETIME CONTRACT: the installed ShortcutManager is expected to outlive this component --
// MainComponent.cpp's explicit `timelinePanel.setShortcutManager(nullptr);` ahead of its own
// member cascade is what makes that true for the real app (`shortcutManager` is declared AFTER
// `timelinePanel`, so it would otherwise destruct first). Every read of `shortcuts_` during normal
// operation (tooltips, keyPressed) still trusts that contract.
//
// This DESTRUCTOR is different: it runs unconditionally, including in tests that forgot the
// explicit detach (a locally-scoped ShortcutManager
// declared AFTER a locally-scoped component, so the manager destructs FIRST at scope exit).
// `shortcuts_` alone can't tell a live manager from a dangling one, so the destructor resolves
// through `shortcutsWeak_` instead — a genuine guard against exactly that bug class,
// rather than one more call site relying on every future test remembering the idiom.
// `shortcutsWeak_` goes null automatically the moment the referenced manager destructs, making
// removeChangeListener safe to skip instead of a use-after-free.
// Shared by the piano roll and the drum machine, each passing the clip it is open on: only the note-ON resolves a
// track (an invalid `clip` silences it), and the OFF follows the latched track as described above.
void TimelinePanelComponent::auditionClipNote(synth::ClipId clip, int pitch, float velocity01, bool on) {
    if (trackHeaderHost_ == nullptr || doc_ == nullptr)
        return;
    const int velocity = juce::jlimit(1, 127, (int)std::lround(velocity01 * 127.0f));
    if (!on) {
        const auto latched = auditionTrackLatch_;
        auditionTrackLatch_ = {};
        if (latched.isValid())
            trackHeaderHost_->auditionTrackNote(latched, pitch, velocity, false);
        return;
    }
    const auto* track = clip.isValid() ? doc_->getTrackForClip(clip) : nullptr;
    if (track == nullptr)
        return;
    auditionTrackLatch_ = track->id;
    trackHeaderHost_->auditionTrackNote(track->id, pitch, velocity, true);
}

TimelinePanelComponent::~TimelinePanelComponent() {
    if (doc_ != nullptr)
        doc_->removeListener(this);
    if (shortcutsWeak_ != nullptr)
        shortcutsWeak_->removeChangeListener(this);
    sidePane_.removeChangeListener(this);
}

// Resolution is strict once a manager IS installed, exactly as on PianoRollComponent: an action
// whose binding is unset or invalid (including an id this ShortcutManager has never heard of) has
// NO key rather than falling back to its default. Mixing the two would mean a binding the user
// deliberately cleared still fired on its factory key.
//
// Non-const (unlike PianoRollComponent's own copy of this pointer): the tool-strip/snap/follow
// buttons' tooltips are real juce::Button tooltips, which CACHE their text (unlike the roll's
// hand-drawn header, whose tooltip is resolved live on every hover query) -- so this panel
// subscribes as a juce::ChangeListener on the installed manager to rebuild them whenever a binding
// changes, and that requires a non-const ShortcutManager* to add/removeChangeListener on.
// Unsubscribes from whichever manager was previously installed first, so re-installing (or
// clearing, with nullptr) never leaves a stale listener registered.
void TimelinePanelComponent::setShortcutManager(ShortcutManager* manager) {
    if (shortcuts_ != nullptr)
        shortcuts_->removeChangeListener(this);
    shortcuts_ = manager;
    shortcutsWeak_ = manager;
    automationLanes_.setShortcuts(manager); // Move Lane Up/Down read their bindings through it
    if (shortcuts_ != nullptr)
        shortcuts_->addChangeListener(this);
    refreshShortcutTooltips();
    // Every existing track header row resolves its own bare m/s/r through this SAME manager —
    // a freshly built header (syncTrackHeaders()'s rebuild branch) gets it there instead, since it
    // isn't a constructor parameter.
    for (auto* header : trackHeaderList_.headers)
        header->setShortcutManager(shortcuts_);
}

// Two broadcasters: the side pane (opened or closed -- a pane that was shut has not been following the selection) and
// the shortcut manager (a binding changed).
void TimelinePanelComponent::changeListenerCallback(juce::ChangeBroadcaster* source) {
    if (source == &sidePane_)
        refreshRoutingPane();
    else
        refreshShortcutTooltips();
}

// Rebuilds every dynamic shortcut-hint tooltip this panel owns (see synth::shortcutHintFor): the
// seven tool-strip buttons, the snap toggle, the follow-playhead toggle and the transport bar's play/stop,
// record, loop and metronome buttons. Called from the
// constructor (after those buttons exist), setShortcutManager (both install and clear), and
// changeListenerCallback.
void TimelinePanelComponent::refreshShortcutTooltips() {
    // The tool-strip action ids, index-aligned with EditTool — see ShortcutManager::resetToDefaults.
    auto actionIdForTool = [](EditTool tool) -> juce::String {
        switch (tool) {
        case EditTool::Select:
            return "timelineToolSelect";
        case EditTool::Range:
            return "timelineToolRange";
        case EditTool::Split:
            return "timelineToolSplit";
        case EditTool::Glue:
            return "timelineToolGlue";
        case EditTool::Erase:
            return "timelineToolErase";
        case EditTool::Mute:
            return "timelineToolMute";
        case EditTool::Draw:
            return "timelineToolDraw";
        }
        return "timelineToolSelect";
    };
    for (auto tool : kAllEditTools) {
        if (auto* button = toolButtons_[(std::size_t)tool].get()) {
            const auto fallback = juce::KeyPress('0' + editToolKeyDigit(tool), juce::ModifierKeys::noModifiers, 0);
            button->setTooltip(synth::ui::formatShortcutHint(
                editToolName(tool), shortcutHintFor(shortcuts_, actionIdForTool(tool), fallback)));
        }
    }

    addTrackButton_.setTooltip(synth::ui::formatShortcutHint("Add Track: a track or marker; type to search",
                                                             shortcutHintFor(shortcuts_, "openAddTrackMenu", {})));

    const auto selectKey =
        shortcutHintFor(shortcuts_, "timelineToolSelect", juce::KeyPress('1', juce::ModifierKeys::noModifiers, 0));
    clipSizingButton_.setTooltip(
        "What dragging a clip edge does to its notes. Hold Option while dragging to stretch "
        "once." +
        (selectKey.isEmpty() ? juce::String() : " With Select active, " + selectKey + " switches it."));

    snapToggleButton_.setTooltip(synth::ui::formatShortcutHint(
        "Snap on/off",
        shortcutHintFor(shortcuts_, "timelineSnapToggle", juce::KeyPress('j', juce::ModifierKeys::noModifiers, 0))));

    // "Follow playhead" used to carry no key hint at all — the ONE sibling in this strip that
    // didn't say its own shortcut.
    const auto cycleKey = shortcutHintFor(shortcuts_, "timelineFollowModeCycle", {});
    auto followTip =
        synth::ui::formatShortcutHint(juce::String("Follow playhead, ") + followModeName(follow_.mode) + " mode",
                                      shortcutHintFor(shortcuts_, "timelineFollowPlayheadToggle",
                                                      juce::KeyPress('f', juce::ModifierKeys::noModifiers, 0)));
    if (cycleKey.isNotEmpty())
        followTip += juce::String::fromUTF8(" \xc2\xb7 switch mode: ") + cycleKey;
    followPlayheadButton_.setTooltip(followTip + juce::String::fromUTF8(" \xc2\xb7 right-click for the mode menu"));

    // The transport bar's own buttons name their keys too. Record and Metronome have no hardcoded default
    // (no manager: no hint); Loop answers to the timeline's bare L, or to the transport action's binding
    // while L is cleared.
    auto loopKey = shortcutHintFor(shortcuts_, "timelineToggleLoop", juce::KeyPress('l', juce::ModifierKeys(), 0));
    if (loopKey.isEmpty() && shortcuts_ != nullptr)
        loopKey = shortcutHintFor(shortcuts_, "transportToggleLoop", {});
    transportBar_.getLoopButton().setTooltip(synth::ui::formatShortcutHint("Loop", loopKey));
    transportBar_.getPlayStopButton().setTooltip(synth::ui::formatShortcutHint(
        "Play / Stop", shortcutHintFor(shortcuts_, "togglePlayback", juce::KeyPress(juce::KeyPress::spaceKey))));
    const bool recordRollsTransport = appProperties_ == nullptr || appProperties_->getUserSettings() == nullptr ||
                                      appProperties_->getUserSettings()->getBoolValue(kRecordStartsPlaybackKey, true);
    transportBar_.getRecordButton().setTooltip(synth::ui::formatShortcutHint(
        recordRollsTransport ? "Record (arms the first armed track; implies Play)"
                             : "Record (arms the first armed track; the take starts when you press Play)",
        shortcutHintFor(shortcuts_, "transportRecord", {})));
    transportBar_.getMetronomeButton().setTooltip(
        synth::ui::formatShortcutHint("Metronome click (summed after the graph - never recorded or bounced)",
                                      shortcutHintFor(shortcuts_, "transportToggleMetronome", {})));

    refreshShapeTooltips();
}

//==============================================================================
// Forwarded to the ruler and the playhead overlay -- the two sub-components that talk to the
// transport directly -- and to the clip-lane area (it only reads the time signature, for
// Snap::Bar).
void TimelinePanelComponent::setTransport(synth::TransportService* transport) {
    transport_ = transport; // this panel's own copy — see the member's comment
    ruler_.setTransport(transport);
    playhead_.setTransport(transport);
    transportBar_.setTransport(transport);
    clipLaneArea_.setTransport(transport);
    pianoRoll_.setTransport(transport);
    drum_->grid.setTransport(transport);
    automationLanes_.setTransport(transport);
}

// Forwarded straight to the transport bar's own metronome toggle -- see
// TimelineTransportBar::setMetronome.
void TimelinePanelComponent::setMetronome(synth::Metronome* metronome) { transportBar_.setMetronome(metronome); }

// Two jobs: hand the snapshot to the playhead overlay, which owns its 30 Hz playing-only timer
// from the play/stop transitions it sees here (see TimelinePlayheadOverlay.h); and diff the small
// slice of transport state the RULER paints (time signature + loop trio), repainting it only on a
// change. The position is deliberately NOT part of that diff: the playhead is the only thing that
// moves with it, and it repaints its own strip. Same gated idiom as the status bar's 5 Hz poll.
void TimelinePanelComponent::updateFromTransport(const synth::TransportService::PositionSnapshot& snapshot,
                                                 double outputLatencySeconds) {
    ++transportUpdateCount_;
    lastOutputLatencySeconds_ = outputLatencySeconds;
    playhead_.updateFromTransport(snapshot, outputLatencySeconds);
    transportBar_.updateFromTransport(snapshot);
    pianoRoll_.refreshSongKey(); // a following clip's key moves with the song key (one comparison when steady)
    // The lane headers' value readouts ride this same poll (nothing runs while the beat stands still).
    const int visibleTop = (int)std::llround(viewState_.trackScrollY);
    automationLanes_.tickReadouts(snapshot.ppq, visibleTop,
                                  visibleTop + trackHeaderViewport_.getMaximumVisibleHeight());
    // The modulator rows' live values (an edit on the canvas card shows there) ride it too.
    automationLanes_.tickModulatorValues(visibleTop, visibleTop + trackHeaderViewport_.getMaximumVisibleHeight());

    // Follow playhead: keep the (latency-compensated) playhead on screen, per mode. Gated on
    // playing/enabled/roll-closed/ no-drag-in-flight, so a stopped transport, the feature off, the piano roll open (it
    // follows on its own) or a clip drag in progress cost nothing here. No new timer: Page rides this 10 Hz poll,
    // Smooth the playhead's 30 Hz frames.
    drum_->grid.setTransportPlaying(snapshot.playing);
    if (follow_.enabled && snapshot.playing && !clipEditorOpen() && !clipLaneArea_.isDragInProgress() &&
        viewState_.pixelsPerBeat > 0.0) {
        followViewToBeat(playhead_.getDrawnBeat());
    }

    // Nothing else repaints the ruler when the time signature or the loop range changes from
    // OUTSIDE its own mouse gestures (a preset/bundle load, a host tempo map, the transport
    // controls), so this poll is where that is noticed. Diffed, not unconditional: an idle poll
    // repaints nothing.
    const RulerTransportState state{snapshot.timeSigNumerator, snapshot.timeSigDenominator, snapshot.looping,
                                    snapshot.loopStartPpq, snapshot.loopEndPpq};
    if (!hasRulerState_) {
        hasRulerState_ = true;
        rulerState_ = state;
        return;
    }
    if (state == rulerState_)
        return;

    const bool timeSigChanged = state.timeSigNumerator != rulerState_.timeSigNumerator ||
                                state.timeSigDenominator != rulerState_.timeSigDenominator;
    rulerState_ = state;
    ruler_.repaint();
    // The lanes grid's bar spacing comes from the time signature too — but only that, so a mere
    // loop change costs the ruler strip alone.
    if (timeSigChanged)
        repaint();
}

// The panel listens to the doc and rebuilds/refreshes the track headers on every notification --
// that is the ONLY thing that updates them: no timer, no polling. Also forwarded to the clip-lane
// area, which runs the same "set doc, refresh once" seam (TimelineClipLaneArea::setTimelineDoc).
void TimelinePanelComponent::setTimelineDoc(synth::TimelineDoc* doc) {
    if (doc_ == doc)
        return;
    if (drum_ != nullptr)
        drum_->grid.stopClipPreview(); // a loop belongs to the project it was started in
    if (doc_ != nullptr)
        doc_->removeListener(this);
    doc_ = doc;
    if (doc_ != nullptr)
        doc_->addListener(this);
    // Before the header rebuild, so the first layout already carries the lanes' rows. Lane and
    // track ids from the old doc mean nothing against a new one: fold state and selection reset.
    selectedAutomationLane_ = {};
    automationLanes_.setTimelineDoc(doc_);
    routingPane_.setDoc(doc_);
    // A row reads the doc it was built against, and syncTrackHeaders() keeps rows by track id, which a new doc can
    // reuse: every row is built afresh against the new one.
    trackHeaderList_.headers.clear();
    syncTrackHeaders();
    refreshRoutingPane();
    clipLaneArea_.setTimelineDoc(doc_);
    pianoRoll_.setTimelineDoc(doc_);
    drum_->grid.setTimelineDoc(doc_);
    // The ruler draws (and edits) the doc's MARKERS — see TimelineRulerComponent's class comment
    // for why it never listens to the doc itself: this panel's timelineChanged() repaints it.
    ruler_.setTimelineDoc(doc_);
    syncAutomationLanes();
}

// It is MainComponent's existing AppUndoManager that gets forwarded here -- see the header for the
// undo-granularity contract this produces. Same degrade-gracefully contract every other
// non-owning setter here has.
void TimelinePanelComponent::setUndoManager(AppUndoManager* undoManager) {
    undoManager_ = undoManager;
    clipLaneArea_.setUndoManager(undoManager);
    pianoRoll_.setUndoManager(undoManager);
    drum_->grid.setUndoManager(undoManager);
    automationLanes_.setUndoManager(undoManager);
    // Marker drag/rename/recolour/delete are real edits and belong on the same one undo stack.
    ruler_.setUndoManager(undoManager);
}

// The song key field: Detect from notes reads every MIDI track but drum kits, and a hovered key auditions on the
// first selected MIDI track that plays somewhere, else the first such track.
void TimelinePanelComponent::initSongKeyField() {
    auto& songKeyField = transportBar_.getSongKeyField();
    songKeyField.pitchClassWeights = [this] {
        if (doc_ == nullptr)
            return std::array<double, 12>{};
        return synth::songPitchClassWeights(*doc_, [this](const synth::Track& track) {
            return trackHeaderHost_ != nullptr && trackHeaderHost_->isDrumKitTrack(track.id);
        });
    };
    songKeyField.resolveAuditionTrack = [this]() -> std::optional<synth::TrackId> {
        if (doc_ == nullptr || trackHeaderHost_ == nullptr)
            return std::nullopt;
        const auto playable = [this](const synth::Track* track) {
            return track != nullptr && track->kind == synth::TrackKind::Midi && track->bindingUuid.isNotEmpty() &&
                   !trackHeaderHost_->isDrumKitTrack(track->id);
        };
        for (const auto id : selectedTracks())
            if (playable(doc_->getTrack(id)))
                return id;
        for (const auto& track : doc_->getTracks())
            if (playable(&track))
                return track.id;
        return std::nullopt;
    };
    songKeyField.auditionNote = [this](synth::TrackId track, int pitch, int velocity, bool on) {
        if (trackHeaderHost_ != nullptr)
            trackHeaderHost_->auditionTrackNote(track, pitch, velocity, on);
    };
}

} // namespace synth::ui
