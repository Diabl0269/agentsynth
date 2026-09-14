// TimelinePanelComponent.cpp
//
// TimelinePanelComponent's constructor/destructor, shortcut-manager wiring
// (setShortcutManager/refreshShortcutTooltips) and the transport/doc/undo-manager setters
// (setTransport/setMetronome/updateFromTransport/setTimelineDoc/setUndoManager) -- the
// class's core lifecycle and wiring. TimelinePanelComponent is declared in
// TimelinePanelComponent.h; sibling TimelinePanel*.cpp files in this directory hold the
// rest of the class (edit-tool/automation strips, clip clipboard, shortcuts, track
// headers, layout/paint).

#include "TimelinePanelComponent.h"
#include "../../ShortcutManager.h"
#include "../../Transport/TransportService.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
constexpr int kAutomationToolRadioGroupId = 4200;

// The edit-tool strip in the transport bar: six square icon buttons in their own radio group
// (4300 — distinct from the automation strip's 4200, which is a different set of tools entirely
// and must not untoggle these).
constexpr int kEditToolRadioGroupId = 4300;
} // namespace

//==============================================================================
TimelinePanelComponent::TimelinePanelComponent() {
    // T159: makes grabKeyboardFocus() on THIS component (the "timeline" focus region's root)
    // succeed deterministically, rather than depending on JUCE's position-ordered descent into
    // children finding a focus-wanting one. This is deliberately separate from the clip-lane-area
    // and piano-roll's OWN grabKeyboardFocus() calls on mouseDown (docs/shortcuts.md's edit-surface
    // routing) — those still work exactly as before; Cmd+Shift+T / Tab just land on the panel root
    // itself rather than wherever positional descent happened to end up.
    setWantsKeyboardFocus(true);

    addAndMakeVisible(ruler_);

    addAndMakeVisible(addTrackButton_);
    addTrackButton_.setComponentID("timelineAddTrackButton");
    addTrackButton_.setTooltip("Add a MIDI or Audio track");
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
        repaint(gridLanesBounds_);
    };

    // Added before the snap combo so it sits left of it in z-order too (they never overlap,
    // but this keeps tab-order/z-order matching visual left-to-right order).
    addAndMakeVisible(transportBar_);
    transportBar_.setComponentID("timelineTransportBar");

    addAndMakeVisible(snapCombo_);
    snapCombo_.setComponentID("timelineSnapCombo");
    snapCombo_.addItem("Off", 1);
    snapCombo_.addItem("Bar", 2);
    snapCombo_.addItem("1", 3);
    snapCombo_.addItem("1/2", 4);
    snapCombo_.addItem("1/4", 5);
    snapCombo_.addItem("1/8", 6);
    snapCombo_.addItem("1/16", 7);
    snapCombo_.addItem("1/32", 8);
    snapCombo_.addItem("1/64", 9);
    snapCombo_.addItem("1/128", 10);
    snapCombo_.setSelectedId((int)viewState_.snap + 1, juce::dontSendNotification);
    // A pick from the combo is just setSnapValue() with the id decoded — the shortcut layer, the
    // grid cycle and this menu therefore share ONE writer (which is also the one place the choice
    // is persisted and the grid painters are repainted).
    snapCombo_.onChange = [this] { setSnapValue((TimelineViewState::Snap)(snapCombo_.getSelectedId() - 1)); };

    // The edit-tool strip, left of the snap controls in the transport bar (see resized()). Radio
    // buttons rather than a combo: which tool is active has to be readable at a glance mid-edit,
    // and the six glyphs are the row every DAW user already knows.
    for (auto tool : kAllEditTools) {
        auto button = std::make_unique<juce::DrawableButton>(juce::String(editToolName(tool)) + " Tool",
                                                             juce::DrawableButton::ImageOnButtonBackground);
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

    // The snap toggle lives in the transport bar so grid magnetism is discoverable without opening
    // a clip — the same switch the piano roll's header "Q" and the panel-wide Q key flip.
    addAndMakeVisible(snapToggleButton_);
    snapToggleButton_.setComponentID("timelineSnapToggle");
    // Tooltip text is set by refreshShortcutTooltips() below.
    snapToggleButton_.setClickingTogglesState(false); // the shared view state is the truth
    snapToggleButton_.setToggleState(viewState_.snapEnabled, juce::dontSendNotification);
    snapToggleButton_.onClick = [this] { setSnapEnabled(!viewState_.snapEnabled); };

    // Follow-playhead — sits next to the snap toggle, same external-state pattern: the button
    // never owns followPlayhead_, it only mirrors it.
    addAndMakeVisible(followPlayheadButton_);
    followPlayheadButton_.setComponentID("timelineFollowPlayheadToggle");
    // Tooltip text is set by refreshShortcutTooltips() below — it used to be the bare "Follow
    // playhead" with no key hint at all, unlike every one of its siblings.
    followPlayheadButton_.setClickingTogglesState(false);
    followPlayheadButton_.setToggleState(followPlayhead_, juce::dontSendNotification);
    followPlayheadButton_.onClick = [this] { setFollowPlayheadEnabled(!followPlayhead_); };

    // Added after everything else but BEFORE the playhead below, so clips draw above the
    // grid (painted by this component's own paint(), which — as a parent — always paints before
    // its children) and below the playhead.
    addAndMakeVisible(clipLaneArea_);
    clipLaneArea_.onClipDoubleClicked = [this](synth::ClipId id) { openPianoRoll(id); };
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
    pianoRoll_.setComponentID("timelinePianoRoll");
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
        if (trackHeaderHost_ == nullptr || doc_ == nullptr)
            return;
        const int velocity = juce::jlimit(1, 127, (int)std::lround(velocity01 * 127.0f));

        if (!on) {
            // Whatever the ON went to, the OFF follows it. No isOpen() check, no re-resolution.
            const auto latched = auditionTrackLatch_;
            auditionTrackLatch_ = {};
            if (latched.isValid())
                trackHeaderHost_->auditionTrackNote(latched, pitch, velocity, false);
            return;
        }

        if (!pianoRoll_.isOpen())
            return;
        const auto* track = doc_->getTrackForClip(pianoRoll_.getClipId());
        if (track == nullptr)
            return;
        auditionTrackLatch_ = track->id;
        trackHeaderHost_->auditionTrackNote(track->id, pitch, velocity, true);
    };

    // Automation strip. All start invisible — resized()/showAutomationLane()/
    // closeAutomationStrip() are the only things that flip visibility, driven by
    // automationStripVisible_.
    addChildComponent(automationEditor_);
    automationEditor_.setComponentID("timelineAutomationEditor");

    auto setUpToolButton = [this](juce::TextButton& button, const juce::String& glyph, const char* componentId,
                                  synth::ui::AutomationLaneEditor::Tool tool) {
        addChildComponent(button);
        button.setComponentID(componentId);
        button.setButtonText(glyph);
        button.setClickingTogglesState(true);
        button.setRadioGroupId(kAutomationToolRadioGroupId);
        button.onClick = [this, tool] { automationEditor_.setTool(tool); };
    };
    setUpToolButton(automationToolPointerButton_, "P", "automationToolPointer",
                    synth::ui::AutomationLaneEditor::Tool::Pointer);
    setUpToolButton(automationToolPencilButton_, juce::String::fromUTF8("\xE2\x9C\x8E"), "automationToolPencil",
                    synth::ui::AutomationLaneEditor::Tool::Pencil);
    setUpToolButton(automationToolLineButton_, juce::String::fromUTF8("\xE2\x95\xB1"), "automationToolLine",
                    synth::ui::AutomationLaneEditor::Tool::Line);
    setUpToolButton(automationToolEraserButton_, juce::String::fromUTF8("\xE2\x8C\xAB"), "automationToolEraser",
                    synth::ui::AutomationLaneEditor::Tool::Eraser);
    automationToolPointerButton_.setToggleState(true, juce::dontSendNotification);

    addChildComponent(laneCombo_);
    laneCombo_.setComponentID("automationLaneCombo");
    laneCombo_.onChange = [this] { applyAutomationLaneMenuChoice(laneCombo_.getSelectedId()); };

    addChildComponent(recordModeCombo_);
    recordModeCombo_.setComponentID("automationRecordModeCombo");
    recordModeCombo_.addItem("Off", 1);
    recordModeCombo_.addItem("Read", 2);
    recordModeCombo_.addItem("Touch", 3);
    recordModeCombo_.addItem("Latch", 4);
    recordModeCombo_.addItem("Write", 5);
    recordModeCombo_.onChange = [this] { applyAutomationRecordModeChoice(recordModeCombo_.getSelectedId()); };

    addChildComponent(automationCloseButton_);
    automationCloseButton_.setComponentID("automationCloseButton");
    automationCloseButton_.setButtonText(juce::String::fromUTF8("\xE2\x9C\x95"));
    automationCloseButton_.onClick = [this] { closeAutomationStrip(); };

    // No transport-bar or automation-strip chrome may steal keyboard focus on click:
    // MainComponent::resolveEditSurface() reads REAL focus, so clicking the snap toggle (or any
    // other chrome control) would otherwise silently reroute the very next Cmd+X/C/V/D from the
    // clips to the graph — the same failure the edit-tool strip above opts out of. juce::Button
    // and a non-editable juce::ComboBox both enable click-grabs-focus in their constructors, so
    // this is an explicit opt-out. Focusability itself is left alone: a combo tabbed to on
    // purpose still takes focus; only the incidental mouse-click grab is disabled.
    for (juce::Component* chrome :
         {static_cast<juce::Component*>(&addTrackButton_), static_cast<juce::Component*>(&snapToggleButton_),
          static_cast<juce::Component*>(&followPlayheadButton_), static_cast<juce::Component*>(&snapCombo_),
          static_cast<juce::Component*>(&automationToolPointerButton_),
          static_cast<juce::Component*>(&automationToolPencilButton_),
          static_cast<juce::Component*>(&automationToolLineButton_),
          static_cast<juce::Component*>(&automationToolEraserButton_),
          static_cast<juce::Component*>(&automationCloseButton_), static_cast<juce::Component*>(&laneCombo_),
          static_cast<juce::Component*>(&recordModeCombo_)})
        chrome->setMouseClickGrabsKeyboardFocus(false);

    // Added LAST so it is topmost — it draws over the ruler, the lanes grid AND the clips.
    addAndMakeVisible(playhead_);
    playhead_.setComponentID("timelinePlayhead");
    // The piano roll maps beats to x through its OWN zoom/scroll, so while it is open the overlay
    // hands it the drawn beat and leaves its rows alone entirely — one timer, two mappings. The
    // region itself is set in resized(), which is the only place the offset is known.
    playhead_.setLocalPlayheadClient(&pianoRoll_);

    // After the playhead, so the top few pixels always belong to the resize gesture rather than to
    // the transport controls underneath. It never overlaps the playhead (which starts below the
    // transport-bar strip).
    addAndMakeVisible(resizeHandle_);
    resizeHandle_.setComponentID("timelineResizeHandle");

    // Every tool button, snapToggleButton_ and followPlayheadButton_ now exist — set their initial
    // (no-manager-installed, hardcoded-default) tooltip text. setShortcutManager re-runs this once
    // a real manager is wired, and again on every bindings-changed notification.
    refreshShortcutTooltips();
}

TimelinePanelComponent::~TimelinePanelComponent() {
    if (doc_ != nullptr)
        doc_->removeListener(this);
    if (shortcuts_ != nullptr)
        shortcuts_->removeChangeListener(this);
}

void TimelinePanelComponent::setShortcutManager(ShortcutManager* manager) {
    if (shortcuts_ != nullptr)
        shortcuts_->removeChangeListener(this);
    shortcuts_ = manager;
    if (shortcuts_ != nullptr)
        shortcuts_->addChangeListener(this);
    refreshShortcutTooltips();
    // T161: every existing track header row resolves its own bare m/s/r through this SAME manager —
    // a freshly built header (syncTrackHeaders()'s rebuild branch) gets it there instead, since it
    // isn't a constructor parameter.
    for (auto* header : trackHeaderList_.headers)
        header->setShortcutManager(shortcuts_);
}

void TimelinePanelComponent::changeListenerCallback(juce::ChangeBroadcaster*) { refreshShortcutTooltips(); }

void TimelinePanelComponent::refreshShortcutTooltips() {
    // The tool-strip action ids, index-aligned with EditTool — see ShortcutManager::resetToDefaults.
    auto actionIdForTool = [](EditTool tool) -> juce::String {
        switch (tool) {
        case EditTool::Select:
            return "timelineToolSelect";
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

    snapToggleButton_.setTooltip(synth::ui::formatShortcutHint(
        "Snap on/off",
        shortcutHintFor(shortcuts_, "timelineSnapToggle", juce::KeyPress('j', juce::ModifierKeys::noModifiers, 0))));

    // "Follow playhead" used to carry no key hint at all — the ONE sibling in this strip that
    // didn't say its own shortcut.
    followPlayheadButton_.setTooltip(synth::ui::formatShortcutHint(
        "Follow playhead", shortcutHintFor(shortcuts_, "timelineFollowPlayheadToggle",
                                           juce::KeyPress('f', juce::ModifierKeys::noModifiers, 0))));
}

//==============================================================================
void TimelinePanelComponent::setTransport(synth::TransportService* transport) {
    transport_ = transport; // this panel's own copy — see the member's comment
    ruler_.setTransport(transport);
    playhead_.setTransport(transport);
    transportBar_.setTransport(transport);
    clipLaneArea_.setTransport(transport);
    pianoRoll_.setTransport(transport);
    automationEditor_.setTransport(transport);
}

void TimelinePanelComponent::setMetronome(synth::Metronome* metronome) { transportBar_.setMetronome(metronome); }

void TimelinePanelComponent::updateFromTransport(const synth::TransportService::PositionSnapshot& snapshot,
                                                 double outputLatencySeconds) {
    ++transportUpdateCount_;
    playhead_.updateFromTransport(snapshot, outputLatencySeconds);
    transportBar_.updateFromTransport(snapshot);

    // Follow playhead: page-flip the view so the (latency-compensated) playhead stays on screen —
    // gated on all four of playing/enabled/roll-closed/no-drag-in-flight, so a stopped transport,
    // the feature switched off, the piano roll open (its own follow wiring lands in a later wave)
    // or a clip drag in progress all cost zero work here. No new timer: this rides the SAME 10 Hz
    // poll every other transport-driven repaint in this class does.
    if (followPlayhead_ && snapshot.playing && !pianoRoll_.isOpen() && !clipLaneArea_.isDragInProgress() &&
        viewState_.pixelsPerBeat > 0.0) {
        const double playheadBeat = playhead_.getDrawnBeat();
        const double visibleBeats = (double)gridLanesBounds_.getWidth() / viewState_.pixelsPerBeat;
        const double lastVisibleBeat = viewState_.firstVisibleBeat + visibleBeats;
        if (playheadBeat < viewState_.firstVisibleBeat || playheadBeat > lastVisibleBeat) {
            // The playhead lands ~10% into the new page rather than flush against its left edge,
            // so the music that follows it is immediately visible instead of starting at the seam.
            viewState_.firstVisibleBeat = std::max(0.0, playheadBeat - 0.1 * visibleBeats);
            ruler_.repaint();
            repaint();
        }
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

void TimelinePanelComponent::setTimelineDoc(synth::TimelineDoc* doc) {
    if (doc_ == doc)
        return;
    if (doc_ != nullptr)
        doc_->removeListener(this);
    doc_ = doc;
    if (doc_ != nullptr)
        doc_->addListener(this);
    syncTrackHeaders();
    clipLaneArea_.setTimelineDoc(doc_);
    pianoRoll_.setTimelineDoc(doc_);
    automationEditor_.setTimelineDoc(doc_);
    // The ruler draws (and edits) the doc's MARKERS — see TimelineRulerComponent's class comment
    // for why it never listens to the doc itself: this panel's timelineChanged() repaints it.
    ruler_.setTimelineDoc(doc_);
    // A lane id selected against the OLD doc can't mean anything against a new one (a fresh
    // preset/bundle load, or the flag-OFF null-doc case) — close outright rather than trying to
    // re-resolve it.
    automationStripVisible_ = false;
    selectedAutomationLane_ = {};
    automationEditor_.setActiveLane({});
}

void TimelinePanelComponent::setUndoManager(AppUndoManager* undoManager) {
    undoManager_ = undoManager;
    clipLaneArea_.setUndoManager(undoManager);
    pianoRoll_.setUndoManager(undoManager);
    automationEditor_.setUndoManager(undoManager);
    // Marker drag/rename/recolour/delete are real edits and belong on the same one undo stack.
    ruler_.setUndoManager(undoManager);
}

} // namespace synth::ui
