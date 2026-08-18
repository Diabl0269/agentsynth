#include "TimelinePanelComponent.h"
#include "../AppUndoManager.h"
#include "../Transport/TransportService.h"
#include "Theme/AppLookAndFeel.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
// Same adaptive-density beat-tick threshold TimelineRulerComponent uses for its own beat ticks —
// duplicated (not shared) because it's a one-line, purely-cosmetic constant and the grid lives on
// the panel while the ticks live on the ruler.
constexpr double kMinBeatLinePixelsPerBeat = 8.0;

// Wheel tuning. Cmd+wheel zoom is exponential in deltaY so equal-and-opposite wheel gestures
// exactly cancel (factor(-d) == 1/factor(d)); plain wheel scroll moves a constant PIXEL distance
// per wheel unit, converted to beats at the CURRENT zoom ("natural": the same physical gesture
// covers less musical time when zoomed in).
constexpr double kZoomWheelSensitivity = 2.0;
constexpr double kScrollPixelsPerWheelUnit = 200.0;

constexpr int kSnapComboWidth = 90;
constexpr const char* kTimelineSnapPropertyKey = "timelineSnap";

// the "+ Track" strip at the top of the track-header column. Fixed height — the headers
// below it scroll, the button never does.
constexpr int kAddTrackButtonHeight = 22;

// Automation strip chrome geometry. Code-only (mirrors the rest of this file's literal
// fallbacks); the strip's own height comes from the themed Metrics::timelineAutomationStripHeight.
constexpr int kAutomationStripHeaderHeight = 24;
constexpr int kAutomationToolButtonWidth = 24;
constexpr int kAutomationRecordModeComboWidth = 90;
constexpr int kAutomationCloseButtonWidth = 24;
constexpr int kAutomationToolRadioGroupId = 4200;
} // namespace

//==============================================================================
TimelinePanelComponent::TimelinePanelComponent() {
    addAndMakeVisible(ruler_);

    addAndMakeVisible(addTrackButton_);
    addTrackButton_.setComponentID("timelineAddTrackButton");
    addTrackButton_.setTooltip("Add a MIDI or Audio track");
    addTrackButton_.onClick = [this] { showAddTrackMenu(); };

    addAndMakeVisible(trackHeaderViewport_);
    trackHeaderViewport_.setComponentID("timelineTrackHeaderViewport");
    trackHeaderViewport_.setScrollBarsShown(true, false);
    trackHeaderViewport_.setViewedComponent(&trackHeaderList_, false);

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
    snapCombo_.setSelectedId((int)viewState_.snap + 1, juce::dontSendNotification);
    snapCombo_.onChange = [this] {
        viewState_.snap = (TimelineViewState::Snap)(snapCombo_.getSelectedId() - 1);
        persistSnapChoice();
        ruler_.repaint();
        // The roll's gridlines and its new-note length both come from the division — a snap change
        // is the only thing that moves them, so this is where they are redrawn.
        pianoRoll_.repaint();
    };

    // Added after everything else but BEFORE the playhead below, so clips draw above the
    // grid (painted by this component's own paint(), which — as a parent — always paints before
    // its children) and below the playhead.
    addAndMakeVisible(clipLaneArea_);
    clipLaneArea_.onClipDoubleClicked = [this](synth::ClipId id) { openPianoRoll(id); };

    // Same slot as clipLaneArea_ (added right after it, before the playhead), but starts
    // invisible — addChildComponent (not addAndMakeVisible) keeps it hidden until openPianoRoll()
    // shows it.
    addChildComponent(pianoRoll_);
    pianoRoll_.setComponentID("timelinePianoRoll");
    pianoRoll_.onCloseRequested = [this] { closePianoRoll(); };

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
}

TimelinePanelComponent::~TimelinePanelComponent() {
    if (doc_ != nullptr)
        doc_->removeListener(this);
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
}

void TimelinePanelComponent::openPianoRoll(synth::ClipId id) {
    pianoRoll_.openClip(id);
    if (!pianoRoll_.isOpen())
        return; // id didn't resolve to a live clip — PianoRollComponent::openClip's own contract

    clipLaneArea_.setVisible(false);
    pianoRoll_.setVisible(true);
    pianoRoll_.grabKeyboardFocus();
    // Which rows of the overlay are still its own just changed — one repaint, on a user action,
    // never per tick.
    playhead_.repaint();
}

void TimelinePanelComponent::closePianoRoll() {
    pianoRoll_.closeRoll();
    pianoRoll_.setVisible(false);
    clipLaneArea_.setVisible(true);
    clipLaneArea_.grabKeyboardFocus();
    playhead_.repaint(); // the overlay owns its whole rect again
}

//==============================================================================
// ---- Automation strip ----

void TimelinePanelComponent::showAutomationLane(synth::LaneId id) {
    if (doc_ == nullptr || doc_->getLane(id) == nullptr)
        return;

    selectedAutomationLane_ = id;
    automationStripVisible_ = true;
    automationEditor_.setTimelineDoc(doc_);
    automationEditor_.setActiveLane(id);
    syncAutomationLaneCombo();
    syncAutomationRecordModeCombo();
    resized();
    repaint();
}

void TimelinePanelComponent::closeAutomationStrip() {
    if (!automationStripVisible_)
        return;
    automationStripVisible_ = false;
    resized();
    repaint();
}

std::vector<TimelinePanelComponent::AutomationLaneOption> TimelinePanelComponent::collectAutomationLaneOptions() const {
    std::vector<AutomationLaneOption> options;
    if (doc_ == nullptr)
        return options;

    for (const auto& track : doc_->getTracks()) {
        for (const auto& lane : track.lanes) {
            juce::String nodeLabel =
                trackHeaderHost_ != nullptr ? trackHeaderHost_->getNodeDisplayName(lane.nodeUuid) : juce::String();
            if (nodeLabel.isEmpty())
                nodeLabel = lane.nodeUuid.substring(0, 8); // uuid-head fallback
            options.push_back({lane.id, nodeLabel + " \xC2\xB7 " + lane.paramId, false, {}});
        }
    }

    // "Add lane..." entries for hosted-plugin instance parameters that have none yet, listed
    // after every existing lane.
    if (trackHeaderHost_ != nullptr) {
        for (auto& addOption : trackHeaderHost_->getAvailablePluginLaneOptions()) {
            AutomationLaneOption option;
            option.label = "Add: " + addOption.label;
            option.isAddEntry = true;
            option.addOption = addOption;
            options.push_back(std::move(option));
        }
    }
    return options;
}

void TimelinePanelComponent::syncAutomationLaneCombo() {
    laneCombo_.clear(juce::dontSendNotification);
    const auto options = collectAutomationLaneOptions();
    int selectedId = 0;
    for (int i = 0; i < (int)options.size(); ++i) {
        laneCombo_.addItem(options[(size_t)i].label, i + 1);
        if (options[(size_t)i].id == selectedAutomationLane_)
            selectedId = i + 1;
    }
    laneCombo_.setSelectedId(selectedId, juce::dontSendNotification);
}

void TimelinePanelComponent::syncAutomationRecordModeCombo() {
    int selectedId = 2; // Read — TimelineDoc's own default for a lane with no explicit mode set
    if (const auto* lane = doc_ != nullptr ? doc_->getLane(selectedAutomationLane_) : nullptr)
        selectedId = lane->recordMode + 1;
    recordModeCombo_.setSelectedId(selectedId, juce::dontSendNotification);
}

void TimelinePanelComponent::applyAutomationLaneMenuChoice(int selectedId) {
    const auto options = collectAutomationLaneOptions();
    if (selectedId < 1 || selectedId > (int)options.size())
        return;
    const auto& chosen = options[(size_t)(selectedId - 1)];
    if (chosen.isAddEntry) {
        // Creates (find-or-create) the lane, then shows it — same shape as choosing an
        // existing entry, just with one extra step first.
        if (trackHeaderHost_ == nullptr)
            return;
        const auto laneId = trackHeaderHost_->addPluginAutomationLane(chosen.addOption);
        if (laneId.isValid())
            showAutomationLane(laneId);
        return;
    }
    showAutomationLane(chosen.id);
}

void TimelinePanelComponent::applyAutomationRecordModeChoice(int selectedId) {
    if (doc_ == nullptr || !selectedAutomationLane_.isValid())
        return;
    const int mode = selectedId - 1;
    const auto laneId = selectedAutomationLane_;
    auto mutate = [this, laneId, mode] { doc_->setLaneRecordMode(laneId, mode); };
    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();
}

//==============================================================================
// ---- Clip clipboard ----

double TimelinePanelComponent::currentBeatsPerBarForPaste() const {
    double beatsPerBar = 4.0;
    if (transport_ != nullptr) {
        const auto snap = transport_->getPositionSnapshot();
        const double tsBeatsPerBar = (double)snap.timeSigNumerator * 4.0 / (double)std::max(1, snap.timeSigDenominator);
        if (tsBeatsPerBar > 0.0)
            beatsPerBar = tsBeatsPerBar;
    }
    return beatsPerBar;
}

bool TimelinePanelComponent::copySelectedClips() {
    if (doc_ == nullptr)
        return false;

    const auto selected = clipSelection_.getSelected(); // ascending id order
    if (selected.empty())
        return false;

    // Pass 1: the earliest selected clip's start — every captured entry is stored relative to it.
    bool haveEarliest = false;
    double earliestStart = 0.0;
    for (auto id : selected) {
        const auto* clip = doc_->getClip(id);
        if (clip == nullptr)
            continue;
        if (!haveEarliest || clip->startBeat < earliestStart) {
            earliestStart = clip->startBeat;
            haveEarliest = true;
        }
    }
    if (!haveEarliest)
        return false; // every selected id was stale

    // Pass 2: capture each clip relative to that start.
    std::vector<ClipboardClip> captured;
    for (auto id : selected) {
        const auto* clip = doc_->getClip(id);
        const auto* track = doc_->getTrackForClip(id);
        if (clip == nullptr || track == nullptr)
            continue;
        ClipboardClip entry;
        entry.originalTrack = track->id;
        entry.relativeStartBeat = clip->startBeat - earliestStart;
        entry.lengthBeats = clip->lengthBeats;
        entry.name = clip->name;
        entry.notes = clip->notes;
        captured.push_back(std::move(entry));
    }
    if (captured.empty())
        return false;

    clipClipboard_ = std::move(captured);
    return true;
}

bool TimelinePanelComponent::pasteClipsAtPlayhead() {
    if (doc_ == nullptr || clipClipboard_.empty())
        return false;

    double playheadBeat = 0.0;
    if (transport_ != nullptr)
        playheadBeat = transport_->getPositionSnapshot().ppq;
    const double snappedPlayhead = viewState_.snapBeat(playheadBeat, currentBeatsPerBarForPaste());

    // Resolved ONCE, before the mutation: the doc's first Midi-kind track, if any — the fallback
    // target for a clip whose original track no longer exists.
    synth::TrackId fallbackTrack;
    for (const auto& track : doc_->getTracks()) {
        if (track.kind == synth::TrackKind::Midi) {
            fallbackTrack = track.id;
            break;
        }
    }

    std::vector<synth::ClipId> newIds;
    auto mutate = [this, snappedPlayhead, fallbackTrack, &newIds] {
        for (const auto& entry : clipClipboard_) {
            synth::TrackId targetTrack = entry.originalTrack;
            if (doc_->getTrack(targetTrack) == nullptr)
                targetTrack = fallbackTrack;
            if (!targetTrack.isValid())
                continue; // no original track and nothing to fall back to — skip this clip

            const double startBeat = std::max(0.0, snappedPlayhead + entry.relativeStartBeat);
            const auto newId = doc_->addClip(targetTrack, startBeat, entry.lengthBeats, entry.name);
            if (!newId.isValid())
                continue;
            for (const auto& note : entry.notes)
                doc_->addNote(newId, note);
            newIds.push_back(newId);
        }
    };

    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();

    if (newIds.empty())
        return false;

    clipSelection_.setSelection(newIds);
    return true;
}

bool TimelinePanelComponent::duplicateSelectedClips() {
    if (doc_ == nullptr)
        return false;

    const auto selected = clipSelection_.getSelected();
    if (selected.empty())
        return false;

    std::vector<synth::ClipId> newIds;
    auto mutate = [this, &selected, &newIds] {
        for (auto id : selected) {
            const auto newId = doc_->duplicateClip(id);
            if (newId.isValid())
                newIds.push_back(newId);
        }
    };

    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();

    if (newIds.empty())
        return false;

    clipSelection_.setSelection(newIds);
    return true;
}

bool TimelinePanelComponent::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey && automationStripVisible_) {
        closeAutomationStrip();
        return true;
    }
    return false;
}

void TimelinePanelComponent::setTrackHeaderHost(TrackHeaderHost* host) {
    trackHeaderHost_ = host;
    // Headers are constructed with the host, so any that already exist have to be rebuilt against
    // the new one rather than refreshed.
    trackHeaderList_.headers.clear();
    syncTrackHeaders();
}

void TimelinePanelComponent::applyAddTrackMenuChoice(int menuId) {
    if (trackHeaderHost_ == nullptr)
        return;

    if (menuId == kAddMidiTrackMenuId)
        trackHeaderHost_->addMidiTrack();
    else if (menuId == kAddAudioTrackMenuId)
        trackHeaderHost_->addAudioTrack();
}

void TimelinePanelComponent::showAddTrackMenu() {
    juce::PopupMenu menu;
    menu.addItem(kAddMidiTrackMenuId, "MIDI Track");
    menu.addItem(kAddAudioTrackMenuId, "Audio Track");

    juce::Component::SafePointer<TimelinePanelComponent> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&addTrackButton_), [safeThis](int result) {
        if (auto* self = safeThis.getComponent())
            self->applyAddTrackMenuChoice(result);
    });
}

void TimelinePanelComponent::timelineChanged(const synth::TimelineDoc&) {
    syncTrackHeaders();
    clipLaneArea_.refreshFromDoc();
    // If the roll is open on a clip this mutation just removed, refreshFromDoc() closes it
    // itself and fires onCloseRequested -> closePianoRoll() (wired in the constructor), which is
    // what swaps clipLaneArea_ back into view.
    pianoRoll_.refreshFromDoc();

    // If the strip is open, re-derive it from the doc — the SAME "refresh, don't poll"
    // discipline every other timeline sub-component follows. A mutation that removed the selected
    // lane closes the strip outright (there is nothing left to show); anything else just repaints
    // the curve and re-syncs the two pickers (a lane could have been added/removed elsewhere, or
    // its recordMode could have changed from under us — AutomationRecorder's own Write-drops-to-
    // Touch-on-stop).
    if (automationStripVisible_) {
        if (doc_ == nullptr || doc_->getLane(selectedAutomationLane_) == nullptr) {
            closeAutomationStrip();
        } else {
            syncAutomationLaneCombo();
            syncAutomationRecordModeCombo();
            automationEditor_.repaint();
        }
    }
}

void TimelinePanelComponent::syncTrackHeaders() {
    if (doc_ == nullptr) {
        if (!trackHeaderList_.headers.isEmpty()) {
            trackHeaderList_.headers.clear();
            layoutTrackHeaders();
        }
        return;
    }

    const auto& tracks = doc_->getTracks();

    // Rebuild only when the SET of tracks changed. A mute toggle, a rename or a re-bind must not
    // destroy and re-create every row (it would drop an in-progress name edit and churn the UI).
    bool sameTracks = (int)tracks.size() == trackHeaderList_.headers.size();
    if (sameTracks) {
        for (int i = 0; i < (int)tracks.size(); ++i) {
            if (!(trackHeaderList_.headers.getUnchecked(i)->getTrackId() == tracks[(size_t)i].id)) {
                sameTracks = false;
                break;
            }
        }
    }

    if (sameTracks) {
        for (auto* header : trackHeaderList_.headers)
            header->refreshFromDoc();
        return;
    }

    trackHeaderList_.headers.clear();
    for (const auto& track : tracks) {
        auto* header =
            trackHeaderList_.headers.add(new TimelineTrackHeaderComponent(*doc_, track.id, trackHeaderHost_));
        trackHeaderList_.addAndMakeVisible(header);
    }
    layoutTrackHeaders();
}

void TimelinePanelComponent::layoutTrackHeaders() {
    // Themed with a literal fallback, same pattern as resized() above — and the SAME token
    // synth::ui::TimelineClipLaneArea reads for its own row height, so header rows and clip rows
    // never drift apart.
    int rowHeight = TimelineTrackHeaderComponent::kRowHeight;
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
        rowHeight = lf->getTheme().metrics.timelineTrackRowHeight;

    const int count = trackHeaderList_.headers.size();
    const int width = std::max(0, trackHeaderViewport_.getMaximumVisibleWidth());

    trackHeaderList_.setSize(width, std::max(count * rowHeight, trackHeaderViewport_.getMaximumVisibleHeight()));
    for (int i = 0; i < count; ++i)
        trackHeaderList_.headers.getUnchecked(i)->setBounds(0, i * rowHeight, width, rowHeight);
}

void TimelinePanelComponent::setApplicationProperties(juce::ApplicationProperties* props) {
    appProperties_ = props;
    if (appProperties_ == nullptr || appProperties_->getUserSettings() == nullptr)
        return;

    int saved = appProperties_->getUserSettings()->getIntValue(kTimelineSnapPropertyKey, (int)viewState_.snap);
    saved = juce::jlimit((int)TimelineViewState::Snap::Off, (int)TimelineViewState::Snap::Sixteenth, saved);
    viewState_.snap = (TimelineViewState::Snap)saved;
    snapCombo_.setSelectedId(saved + 1, juce::dontSendNotification);

    // A pure forward — the transport bar owns and persists its own two keys. See this
    // method's header comment.
    transportBar_.setApplicationProperties(props);
}

void TimelinePanelComponent::persistSnapChoice() {
    if (appProperties_ == nullptr || appProperties_->getUserSettings() == nullptr)
        return;
    appProperties_->getUserSettings()->setValue(kTimelineSnapPropertyKey, (int)viewState_.snap);
    appProperties_->saveIfNeeded();
}

//==============================================================================
void TimelinePanelComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    // Reproject into the ruler's coordinate space regardless of whether the event originated on
    // this component or bubbled up from the ruler child — both share the same x == 0 origin as
    // TimelineViewState (the lanes/ruler content start), so this is exactly the anchor
    // beatToX/xToBeat expect.
    const double anchorX = (double)e.getEventRelativeTo(&ruler_).position.x;

    if (e.mods.isCommandDown()) {
        const double factor = std::exp((double)wheel.deltaY * kZoomWheelSensitivity);
        viewState_.zoomAroundX(factor, anchorX);
    } else {
        const double deltaBeats = -(double)wheel.deltaY * kScrollPixelsPerWheelUnit / viewState_.pixelsPerBeat;
        viewState_.scrollBeats(deltaBeats);
    }

    ruler_.repaint();
    repaint();
}

//==============================================================================
void TimelinePanelComponent::resized() {
    // Themed metrics with literal fallbacks for the headless test path (same pattern as
    // MainComponent::resized()/computePanelBounds()).
    int transportBarHeight = 28;
    int trackHeaderWidth = 160;
    int rulerHeight = 24;
    int automationStripHeight = 72;
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel())) {
        const auto& m = lf->getTheme().metrics;
        transportBarHeight = m.timelineTransportBarHeight;
        trackHeaderWidth = m.timelineTrackHeaderWidth;
        rulerHeight = m.timelineRulerHeight;
        automationStripHeight = m.timelineAutomationStripHeight;
    }

    auto bounds = getLocalBounds();
    transportBarBounds_ = bounds.removeFromTop(transportBarHeight);
    trackHeaderBounds_ = bounds.removeFromLeft(trackHeaderWidth);
    lanesBounds_ = bounds; // remainder

    // The "+ MIDI Track" strip is pinned at the top of the header column, the scrolling header list
    // below it. Both live INSIDE trackHeaderBounds_, so the panel's three regions still tile.
    auto headerColumn = trackHeaderBounds_;
    addTrackButton_.setBounds(headerColumn.removeFromTop(kAddTrackButtonHeight).reduced(2, 1));
    trackHeaderViewport_.setBounds(headerColumn);
    layoutTrackHeaders();

    auto lanes = lanesBounds_;
    ruler_.setBounds(lanes.removeFromTop(rulerHeight));
    gridLanesBounds_ = lanes;

    // The automation strip is carved from the BOTTOM of gridLanesBounds_ (which is what
    // shrinks the clip-lane area/piano roll below), leaving the ruler untouched above.
    if (automationStripVisible_ && gridLanesBounds_.getHeight() > automationStripHeight) {
        automationStripBounds_ = gridLanesBounds_.removeFromBottom(automationStripHeight);
    } else {
        automationStripBounds_ = {};
    }

    // The clip-lane area fills EXACTLY the rect the grid below is painted into (paint()'s
    // gridLanesBounds_ loop, unchanged) — so clips line up with the bar/beat grid pixel-for-pixel.
    clipLaneArea_.setBounds(gridLanesBounds_);
    // The piano roll occupies the SAME rect, unconditionally (whichever of the two is
    // invisible just doesn't paint) — this is also what keeps its beatToX(beat) mapping identical
    // to the clip lanes' and the playhead's (see PianoRollComponent's class comment).
    pianoRoll_.setBounds(gridLanesBounds_);

    // The playhead spans the WHOLE lanes region, ruler included, so the line reads as one stroke
    // from the ruler down through the tracks. Its local x == 0 is lanesBounds_.getX(), which is
    // also the ruler's — i.e. exactly TimelineViewState's origin, so no offset arithmetic is
    // needed anywhere in the overlay. Trimmed by the strip height too, so the line never
    // draws underneath the strip's own chrome.
    playhead_.setBounds(!automationStripBounds_.isEmpty() ? lanesBounds_.withTrimmedBottom(automationStripHeight)
                                                          : lanesBounds_);
    // The piano roll's rect expressed in the OVERLAY's coordinates (both share lanesBounds_'s x
    // origin; only the ruler strip separates their tops). While the roll is open the overlay skips
    // exactly these rows — see TimelinePlayheadOverlay::LocalPlayheadClient.
    playhead_.setLocalPlayheadRegion(gridLanesBounds_ - playhead_.getBounds().getPosition());

    // Strip header row (tool buttons, lane/record-mode pickers, close) above the curve
    // canvas. Visibility follows automationStripVisible_ exactly — nothing else flips it.
    const bool stripOpen = !automationStripBounds_.isEmpty();
    automationEditor_.setVisible(stripOpen);
    automationToolPointerButton_.setVisible(stripOpen);
    automationToolPencilButton_.setVisible(stripOpen);
    automationToolLineButton_.setVisible(stripOpen);
    automationToolEraserButton_.setVisible(stripOpen);
    laneCombo_.setVisible(stripOpen);
    recordModeCombo_.setVisible(stripOpen);
    automationCloseButton_.setVisible(stripOpen);
    if (stripOpen) {
        auto strip = automationStripBounds_;
        auto header = strip.removeFromTop(kAutomationStripHeaderHeight);
        automationToolPointerButton_.setBounds(header.removeFromLeft(kAutomationToolButtonWidth).reduced(2));
        automationToolPencilButton_.setBounds(header.removeFromLeft(kAutomationToolButtonWidth).reduced(2));
        automationToolLineButton_.setBounds(header.removeFromLeft(kAutomationToolButtonWidth).reduced(2));
        automationToolEraserButton_.setBounds(header.removeFromLeft(kAutomationToolButtonWidth).reduced(2));
        automationCloseButton_.setBounds(header.removeFromRight(kAutomationCloseButtonWidth).reduced(2));
        recordModeCombo_.setBounds(header.removeFromRight(kAutomationRecordModeComboWidth).reduced(2));
        laneCombo_.setBounds(header.reduced(2));
        automationEditor_.setBounds(strip);
    }

    // The grab strip runs the panel's full width along its top edge, OVERLAPPING the transport-bar
    // strip — transportBarBounds_ stays the whole strip (the three regions still tile), but the
    // controls inside it are laid out BELOW the handle so a resize grab never lands on a button.
    resizeHandle_.setBounds(0, 0, getWidth(), kResizeHandleHeight);

    // Snap selector: right-hand side of the transport bar. The transport controls (play/stop/
    // record/loop + BPM/time-sig + readout) fill the rest, left-aligned.
    auto transportBar = transportBarBounds_.withTrimmedTop(kResizeHandleHeight);
    snapCombo_.setBounds(transportBar.removeFromRight(kSnapComboWidth).reduced(2));
    transportBar_.setBounds(transportBar);
}

//==============================================================================
void TimelinePanelComponent::paint(juce::Graphics& g) {
    using namespace synth::theme;

    juce::Colour bg, border;
    if (auto* lf = dynamic_cast<AppLookAndFeel*>(&getLookAndFeel())) {
        const auto& c = lf->getTheme().colors;
        bg = c.bg0;
        border = c.border;
    } else {
        bg = juce::Colours::darkgrey.darker(0.5f);
        border = juce::Colours::grey;
    }

    g.fillAll(bg);

    // Thin top border separating the panel from the graph editor above it.
    g.setColour(border);
    g.drawHorizontalLine(0, 0.0f, (float)getWidth());

    // Bar/beat grid across the lanes region (below the ruler), using the SAME shared
    // TimelineViewState the ruler paints from — bar lines stronger, beat lines fainter. No
    // dedicated grid colour tokens exist yet (checked Theme::Colors — nothing grid-specific), so
    // this reuses `border` at two alpha levels rather than adding new tokens for one caller.
    if (gridLanesBounds_.getWidth() > 0 && gridLanesBounds_.getHeight() > 0) {
        synth::TransportService* transport = ruler_.getTransport();
        double beatsPerBar = 4.0;
        if (transport != nullptr) {
            const auto snap = transport->getPositionSnapshot();
            const double tsBeatsPerBar =
                (double)snap.timeSigNumerator * 4.0 / (double)std::max(1, snap.timeSigDenominator);
            if (tsBeatsPerBar > 0.0)
                beatsPerBar = tsBeatsPerBar;
        }

        const double widthPx = (double)gridLanesBounds_.getWidth();
        const double startBeat = viewState_.firstVisibleBeat;
        const double endBeat = viewState_.xToBeat(widthPx);
        const bool drawBeatLines = viewState_.pixelsPerBeat >= kMinBeatLinePixelsPerBeat;
        const int beatsPerBarRounded = std::max(1, (int)std::llround(beatsPerBar));

        const juce::int64 firstBar = (juce::int64)std::floor(startBeat / beatsPerBar) - 1;
        const juce::int64 lastBar = (juce::int64)std::ceil(endBeat / beatsPerBar) + 1;

        const int top = gridLanesBounds_.getY();
        const int bottom = gridLanesBounds_.getBottom();
        const int xOrigin = gridLanesBounds_.getX();

        for (juce::int64 bar = firstBar; bar <= lastBar; ++bar) {
            const double barBeat = (double)bar * beatsPerBar;
            const double x = viewState_.beatToX(barBeat);
            if (x < -1.0 || x > widthPx + 1.0)
                continue;

            g.setColour(border);
            g.drawVerticalLine(xOrigin + (int)std::llround(x), (float)top, (float)bottom);

            if (drawBeatLines) {
                for (int beatInBar = 1; beatInBar < beatsPerBarRounded; ++beatInBar) {
                    const double beatX = viewState_.beatToX(barBeat + (double)beatInBar);
                    if (beatX < 0.0 || beatX > widthPx)
                        continue;
                    g.setColour(border.withAlpha(0.35f));
                    g.drawVerticalLine(xOrigin + (int)std::llround(beatX), (float)top, (float)bottom);
                }
            }
        }
    }
}

//==============================================================================
// ---- Top-edge resize handle ----

TimelinePanelComponent::ResizeHandle::ResizeHandle(TimelinePanelComponent& owner)
    : owner_(owner) {
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void TimelinePanelComponent::ResizeHandle::paint(juce::Graphics& g) {
    juce::Colour line;
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel())) {
        const auto& c = lf->getTheme().colors;
        line = isHighlighted() ? c.accent : c.border;
    } else {
        line = isHighlighted() ? juce::Colours::white : juce::Colours::grey;
    }

    // Idle: exactly the hairline the panel already draws at y == 0, so nothing new is visible until
    // the pointer arrives. Hovered/dragging: the hairline brightens and the strip picks up a faint
    // wash, which is the whole affordance.
    if (isHighlighted())
        g.fillAll(line.withAlpha(0.18f));
    g.setColour(line);
    g.fillRect(0, 0, getWidth(), 1);
}

void TimelinePanelComponent::ResizeHandle::mouseEnter(const juce::MouseEvent&) {
    if (hovered_)
        return; // repaint only on a CHANGE
    hovered_ = true;
    repaint();
}

void TimelinePanelComponent::ResizeHandle::mouseExit(const juce::MouseEvent&) {
    if (!hovered_)
        return;
    hovered_ = false;
    if (!dragging_)
        repaint();
}

int TimelinePanelComponent::ResizeHandle::desiredHeightFor(const juce::MouseEvent& e) const {
    // Absolute, not a delta: the owner moves the panel's top edge (and this handle with it) on
    // every callback, so only the panel's FIXED bottom edge is a stable reference.
    const int topY = e.getScreenPosition().y - grabOffsetY_;
    return owner_.getScreenBounds().getBottom() - topY;
}

void TimelinePanelComponent::ResizeHandle::mouseDown(const juce::MouseEvent& e) {
    const bool wasHighlighted = isHighlighted();
    dragging_ = true;
    moved_ = false;
    grabOffsetY_ = (int)e.getEventRelativeTo(&owner_).position.y;
    lastDesiredHeight_ = desiredHeightFor(e);
    if (!wasHighlighted)
        repaint();
}

void TimelinePanelComponent::ResizeHandle::mouseDrag(const juce::MouseEvent& e) {
    if (!dragging_)
        return;
    moved_ = true;
    lastDesiredHeight_ = desiredHeightFor(e);
    if (owner_.onResizeHeight)
        owner_.onResizeHeight(lastDesiredHeight_);
}

void TimelinePanelComponent::ResizeHandle::mouseUp(const juce::MouseEvent&) {
    if (!dragging_)
        return;
    dragging_ = false;
    if (!hovered_)
        repaint(); // the highlight only changes when the pointer has already left
    // Persist point for the owner — and only for a real drag: a stray click must not write settings.
    if (moved_ && owner_.onResizeHeightCommitted)
        owner_.onResizeHeightCommitted(lastDesiredHeight_);
}

} // namespace synth::ui
