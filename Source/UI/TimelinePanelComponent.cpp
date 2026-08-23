#include "TimelinePanelComponent.h"
#include "../AppUndoManager.h"
#include "../ShortcutManager.h"
#include "../Transport/TransportService.h"
#include "ScrollPolicy.h"
#include "Theme/AppLookAndFeel.h"
#include "UIAnimation.h"
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
constexpr int kSnapToggleButtonWidth = 26;
constexpr int kFollowPlayheadButtonWidth = 26;
constexpr const char* kTimelineSnapPropertyKey = "timelineSnap";
constexpr const char* kTimelineSnapEnabledPropertyKey = "timelineSnapEnabled";
constexpr const char* kTimelineFollowPlayheadPropertyKey = "timelineFollowPlayhead";
// Item: P (loop the selection) also ARMS looping by default; Preferences can turn the arming off
// so P only places the locators (Cubase's behaviour). Read at key time — no cached copy to drift.
constexpr const char* kTimelineLoopSelectionArmsPropertyKey = "timelineLoopSelectionArms";
// The roll's key-label density (PianoRollComponent::KeyLabelMode). "all" (default) labels every
// key row; "c" labels only the Cs. Owned by PreferencesSettingsTab's persistX pattern; read here
// by reloadPianoRollAppearancePrefs().
constexpr const char* kPianoRollKeyLabelsPropertyKey = "pianoRollKeyLabels";

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

// The edit-tool strip in the transport bar: six square icon buttons in their own radio group
// (4300 — distinct from the automation strip's 4200, which is a different set of tools entirely
// and must not untoggle these).
constexpr int kEditToolRadioGroupId = 4300;
constexpr int kEditToolButtonWidth = 24;

// The icon each edit tool's button paints — the SAME glyph synth::ui::makeToolCursor renders the
// tool's cursor from, so button and cursor can never disagree.
synth::theme::Icon iconForEditTool(synth::ui::EditTool tool) noexcept {
    using synth::theme::Icon;
    switch (tool) {
    case synth::ui::EditTool::Select:
        return Icon::ToolSelect;
    case synth::ui::EditTool::Split:
        return Icon::ToolSplit;
    case synth::ui::EditTool::Glue:
        return Icon::ToolGlue;
    case synth::ui::EditTool::Erase:
        return Icon::ToolErase;
    case synth::ui::EditTool::Mute:
        return Icon::ToolMute;
    case synth::ui::EditTool::Draw:
        return Icon::ToolDraw;
    }
    return Icon::ToolSelect;
}

// A bare (unmodified) keypress — the shape every one of this panel's own default bindings has.
juce::KeyPress plainKey(int keyCode) noexcept { return juce::KeyPress(keyCode, juce::ModifierKeys::noModifiers, 0); }

// The ShortcutManager action id that picks `tool`. Derived from editToolName() rather than written
// out as a second table, so adding a tool cannot leave a digit unbound here while EditTool.h,
// the button strip and the tooltips all already know about it — the ids in
// ShortcutManager::resetToDefaults() are exactly "timelineTool" + this name.
juce::String toolActionIdFor(synth::ui::EditTool tool) { return "timelineTool" + juce::String(editToolName(tool)); }
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
        "Snap to grid on/off",
        shortcutHintFor(shortcuts_, "timelineSnapToggle", juce::KeyPress('q', juce::ModifierKeys::noModifiers, 0))));

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

//==============================================================================
// ---- Edit-tool strip ----

juce::DrawableButton* TimelinePanelComponent::getToolButton(EditTool tool) const noexcept {
    return toolButtons_[(std::size_t)tool].get();
}

void TimelinePanelComponent::setActiveTool(EditTool tool) {
    activeTool_ = tool;
    // Both editors, always — they share the lanes rect and swap at will, so a tool that only
    // reached the visible one would silently change meaning the moment a clip was opened.
    clipLaneArea_.setActiveTool(tool);
    pianoRoll_.setActiveTool(tool);
    // Every button is set explicitly rather than leaning on the radio group to untoggle its
    // siblings: this method is also reached from the number keys and from MainComponent, where no
    // button was clicked at all. dontSendNotification, or setting the state would re-enter here
    // through the button's own onClick.
    for (auto candidate : kAllEditTools)
        if (auto* button = getToolButton(candidate))
            button->setToggleState(candidate == tool, juce::dontSendNotification);
}

void TimelinePanelComponent::applyToolStripTheme() {
    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    for (auto tool : kAllEditTools) {
        auto* button = getToolButton(tool);
        if (button == nullptr)
            continue;
        // Null-guarded on BOTH counts: a headless build has no themed LnF, and even with one
        // getIcon returns nullptr when the asset library isn't linked in. The button stays
        // imageless but fully functional in either case.
        if (lf != nullptr) {
            if (auto icon = lf->getIcon(iconForEditTool(tool)))
                button->setImages(icon.get());
            // The active tool's highlight is a BACKGROUND colour, not a different icon tint —
            // the glyph is the same in both states (see AppLookAndFeel::retintIcons).
            button->setColour(juce::DrawableButton::backgroundOnColourId, lf->getTheme().colors.toolActive);
        }
    }

    // Same theme re-skin for the follow-playhead toggle: null-guarded on both counts (no themed
    // LnF in a headless build; getIcon returns nullptr when the asset library isn't linked in), so
    // the button stays imageless but fully functional either way.
    if (lf != nullptr) {
        if (auto icon = lf->getIcon(synth::theme::Icon::FollowPlayhead))
            followPlayheadButton_.setImages(icon.get());
        followPlayheadButton_.setColour(juce::DrawableButton::backgroundOnColourId, lf->getTheme().colors.toolActive);
    }
}

void TimelinePanelComponent::lookAndFeelChanged() { applyToolStripTheme(); }

void TimelinePanelComponent::parentHierarchyChanged() { applyToolStripTheme(); }

//==============================================================================
void TimelinePanelComponent::openPianoRoll(synth::ClipId id) {
    pianoRoll_.openClip(id);
    if (!pianoRoll_.isOpen())
        return; // id didn't resolve to a live clip — PianoRollComponent::openClip's own contract

    clipLaneArea_.setVisible(false);
    pianoRoll_.setVisible(true);
    pianoRoll_.grabKeyboardFocus();
    // The ruler now labels the ROLL's beats (offset by its keys gutter, plus the scale-assist
    // panel's width while THAT is open too — see PianoRollComponent::leftGutterWidth), so the bar
    // numbers above show the edited clip's real timeline position instead of wherever the lanes
    // were scrolled.
    ruler_.setMappingOverride(&pianoRoll_.getRollViewState(), pianoRoll_.leftGutterWidth());
    // Which rows of the overlay are still its own just changed — one repaint, on a user action,
    // never per tick.
    playhead_.repaint();
}

void TimelinePanelComponent::closePianoRoll() {
    pianoRoll_.closeRoll();
    pianoRoll_.setVisible(false);
    clipLaneArea_.setVisible(true);
    clipLaneArea_.grabKeyboardFocus();
    ruler_.setMappingOverride(nullptr, 0); // back to the shared lanes mapping
    playhead_.repaint();                   // the overlay owns its whole rect again
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

synth::TrackId TimelinePanelComponent::firstTrackOfKind(synth::TrackKind kind) const {
    if (doc_ == nullptr)
        return {};
    for (const auto& track : doc_->getTracks())
        if (track.kind == kind)
            return track.id;
    return {};
}

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
        // The payload, not the source row, decides where this can be pasted — see
        // ClipboardClip::requiredKind.
        entry.requiredKind = clip->assetRef.isNotEmpty() ? synth::TrackKind::Audio : synth::TrackKind::Midi;
        entry.relativeStartBeat = clip->startBeat - earliestStart;
        entry.lengthBeats = clip->lengthBeats;
        entry.name = clip->name;
        entry.notes = clip->notes; // MidiNote copies carry each note's own muted flag
        entry.muted = clip->muted;
        entry.assetRef = clip->assetRef;
        entry.gainDb = clip->gainDb;
        entry.fadeInBeats = clip->fadeInBeats;
        entry.fadeOutBeats = clip->fadeOutBeats;
        entry.sourceStartSeconds = clip->sourceStartSeconds;
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

    // Resolved ONCE, before the mutation: the target row for every entry, so the loop below does
    // no lookups against a doc it is halfway through mutating. The original track only counts if
    // it still plays this clip's payload (see ClipboardClip::requiredKind); otherwise the first
    // track of the required kind, and otherwise nothing at all.
    std::vector<synth::TrackId> targets;
    targets.reserve(clipClipboard_.size());
    for (const auto& entry : clipClipboard_) {
        const auto* original = doc_->getTrack(entry.originalTrack);
        targets.push_back(original != nullptr && original->kind == entry.requiredKind
                              ? entry.originalTrack
                              : firstTrackOfKind(entry.requiredKind));
    }

    std::vector<synth::ClipId> newIds;
    auto mutate = [this, snappedPlayhead, &targets, &newIds] {
        for (std::size_t i = 0; i < clipClipboard_.size(); ++i) {
            const auto& entry = clipClipboard_[i];
            if (!targets[i].isValid())
                continue; // nowhere this clip could play — skip it rather than park it

            const double startBeat = std::max(0.0, snappedPlayhead + entry.relativeStartBeat);
            const auto newId = doc_->addClip(targets[i], startBeat, entry.lengthBeats, entry.name);
            if (!newId.isValid())
                continue;
            for (const auto& note : entry.notes)
                doc_->addNote(newId, note);
            // Through the setters, not into the struct: setClipAsset is the gate that rejects an
            // assetRef that is not bundle-relative, and a clipboard is not a trusted source.
            if (entry.assetRef.isNotEmpty() || entry.sourceStartSeconds != 0.0)
                doc_->setClipAsset(newId, entry.assetRef, entry.sourceStartSeconds);
            doc_->setClipGainDb(newId, entry.gainDb);
            doc_->setClipFades(newId, entry.fadeInBeats, entry.fadeOutBeats);
            doc_->setClipMuted(newId, entry.muted);
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

bool TimelinePanelComponent::canCutClips() const noexcept { return doc_ != nullptr && !clipSelection_.isEmpty(); }

bool TimelinePanelComponent::hasClipSelection() const noexcept { return !clipSelection_.isEmpty(); }

bool TimelinePanelComponent::cutSelectedClips() {
    // The copy half also validates (no doc / nothing selected / every id stale all fail there), so
    // nothing is deleted unless something was actually captured.
    if (!copySelectedClips())
        return false;

    const auto selected = clipSelection_.getSelected();
    auto mutate = [this, selected] {
        for (auto id : selected)
            doc_->removeClip(id);
    };
    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();

    clipSelection_.clear();
    clipLaneArea_.repaint();
    return true;
}

bool TimelinePanelComponent::selectAllClips() {
    if (doc_ == nullptr)
        return false;

    std::vector<synth::ClipId> all;
    for (const auto& track : doc_->getTracks())
        for (const auto& clip : track.clips)
            all.push_back(clip.id);
    if (all.empty())
        return false;

    clipSelection_.setSelection(all);
    clipLaneArea_.repaint();
    return true;
}

bool TimelinePanelComponent::repeatSelectedClips(int count) {
    if (doc_ == nullptr || count < 1)
        return false;

    // Snapshot the source geometry BEFORE mutating: every duplicate re-seats its track's clip
    // vector, so a Clip pointer (or a re-read of the selection) taken mid-loop would be stale.
    struct Source {
        synth::ClipId id;
        synth::TrackId track;
        double startBeat = 0.0;
    };
    std::vector<Source> sources;
    bool haveSpan = false;
    double spanStart = 0.0, spanEnd = 0.0;
    for (auto id : clipSelection_.getSelected()) {
        const auto* clip = doc_->getClip(id);
        const auto* track = doc_->getTrackForClip(id);
        if (clip == nullptr || track == nullptr)
            continue;
        sources.push_back({id, track->id, clip->startBeat});
        const double end = clip->startBeat + clip->lengthBeats;
        spanStart = haveSpan ? std::min(spanStart, clip->startBeat) : clip->startBeat;
        spanEnd = haveSpan ? std::max(spanEnd, end) : end;
        haveSpan = true;
    }
    if (!haveSpan || !(spanEnd > spanStart))
        return false;

    const double blockLength = spanEnd - spanStart;

    std::vector<synth::ClipId> newIds;
    auto mutate = [this, sources, blockLength, count, &newIds] {
        for (int repeat = 1; repeat <= count; ++repeat) {
            for (const auto& source : sources) {
                const auto dup = doc_->duplicateClip(source.id);
                if (!dup.isValid())
                    continue;
                // duplicateClip drops the copy one clip-length after its source; moving it to
                // (its own start + n block lengths) is what tiles the whole selection forward.
                // Same track by construction, so the kind check never engages.
                doc_->moveClipToTrack(dup, source.track, source.startBeat + (double)repeat * blockLength);
                newIds.push_back(dup);
            }
        }
    };

    if (undoManager_)
        undoManager_->recordTimelineChange(*doc_, mutate);
    else
        mutate();

    if (newIds.empty())
        return false;

    clipSelection_.setSelection(newIds);
    clipLaneArea_.repaint();
    return true;
}

bool TimelinePanelComponent::matchesAction(const juce::KeyPress& key, const juce::String& actionId,
                                           const juce::KeyPress& fallback) const {
    if (shortcuts_ == nullptr)
        return key == fallback;
    const auto binding = shortcuts_->getBinding(actionId);
    // An invalid binding is "this action has no key": either the user cleared it, or this build's
    // ShortcutManager has never heard of the id (getBinding answers an unknown id with a
    // default-constructed KeyPress). Falling back to `fallback` here would resurrect a key the user
    // deliberately unbound, so it deliberately does not. keyPressMatches rather than == so a user
    // rebind onto a Shift-chorded symbol key survives the macOS peer delivering the SHIFTED
    // character as the key code (see ShortcutManager::keyPressMatches).
    return ShortcutManager::keyPressMatches(binding, key);
}

bool TimelinePanelComponent::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey && automationStripVisible_) {
        closeAutomationStrip();
        return true;
    }

    // Number keys pick a tool, BEFORE the letter keys below.
    //
    // With a ShortcutManager installed each digit is one rebindable action ("timelineToolSplit" and
    // friends), resolved through matchesAction — so an unset id has no key, and Ctrl+Shift+1 (the
    // grid command) can never be mistaken for a bare 1, because juce::KeyPress equality is exact on
    // modifiers.
    //
    // With NO manager (headless tests, embeddings with no settings store) the pre-shortcut behaviour
    // is kept verbatim: the digits come off editToolForKeyChar, command-modified digits are left
    // alone (a host/app menu shortcut owns those), and the digits EditTool.h reserves — 2, 6 and 9 —
    // return false and keep whatever meaning they have elsewhere. That fallback reads the TEXT
    // CHARACTER first, which is what a real keystroke carries on a layout where the digit needs a
    // modifier; the manager path cannot do that, since a modifier there is part of the binding.
    if (shortcuts_ != nullptr) {
        for (auto tool : kAllEditTools) {
            if (matchesAction(key, toolActionIdFor(tool), plainKey('0' + editToolKeyDigit(tool)))) {
                setActiveTool(tool);
                return true;
            }
        }
    } else if (!key.getModifiers().isCommandDown()) {
        const int typed = (int)key.getTextCharacter();
        if (const auto tool = editToolForKeyChar(typed != 0 ? typed : key.getKeyCode())) {
            setActiveTool(tool.value());
            return true;
        }
    }

    // Panel-scoped transport/snap keys. These fire when the key was NOT consumed by the focused
    // child (JUCE bubbles unhandled keys up the parent chain), so they cover every focus target
    // inside the timeline — track headers, the lanes, the roll (which consumes Q itself).

    // Q = toggle grid magnetism (Shift+Q one-shot quantise lives on the roll, where the notes are).
    // Shares "timelineSnapToggle" with the roll: one binding, one key, whichever surface has focus.
    if (matchesAction(key, "timelineSnapToggle", plainKey('q'))) {
        setSnapEnabled(!viewState_.snapEnabled);
        return true;
    }

    // L = toggle looping, keeping the existing bounds — exactly the transport bar's loop button.
    if (matchesAction(key, "timelineToggleLoop", plainKey('l')) && transport_ != nullptr) {
        const auto snap = transport_->getPositionSnapshot();
        transport_->setLoop(snap.loopStartPpq, snap.loopEndPpq, !snap.looping);
        ruler_.repaint();
        return true;
    }

    // F = follow playhead on/off — the transport strip's follow button as a key, panel-scoped for
    // the same reason as Q: it has to work whichever timeline surface has focus, the roll included
    // (setFollowPlayheadEnabled already persists the choice and forwards the flag into the roll).
    if (matchesAction(key, "timelineFollowPlayheadToggle", plainKey('f'))) {
        setFollowPlayheadEnabled(!isFollowPlayheadEnabled());
        return true;
    }

    // P = loop the selection. With the roll open the "selection" is the edited clip; otherwise the
    // lane area already handles P itself when focused — this is the fallback for other focus
    // targets inside the panel (same span, same setLoop the lane's callback performs).
    if (matchesAction(key, "timelineLoopSelection", plainKey('p')) && transport_ != nullptr) {
        std::optional<std::pair<double, double>> span;
        if (pianoRoll_.isOpen() && doc_ != nullptr) {
            if (const auto* clip = doc_->getClip(pianoRoll_.getClipId()))
                span = std::make_pair(clip->startBeat, clip->startBeat + clip->lengthBeats);
        } else {
            span = clipLaneArea_.getSelectedClipSpan();
        }
        if (!span || !(span->second > span->first))
            return false;
        // Whether P also ARMS looping is a preference (default yes); off means "place the
        // locators, keep the current loop state" — Cubase's reading.
        bool arm = true;
        if (appProperties_ != nullptr && appProperties_->getUserSettings() != nullptr)
            arm = appProperties_->getUserSettings()->getBoolValue(kTimelineLoopSelectionArmsPropertyKey, true);
        transport_->setLoop(span->first, span->second, arm || transport_->getPositionSnapshot().looping);
        ruler_.repaint();
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
        // The header only ever reports "the A button was clicked" — this panel is the one that
        // knows whether the strip is already open on this track's lane, so it's the one that
        // decides open vs. close.
        const auto trackId = track.id;
        header->onAutomationToggleRequested = [this, trackId](synth::TrackId) { toggleAutomationForTrack(trackId); };
        trackHeaderList_.addAndMakeVisible(header);
    }
    layoutTrackHeaders();
}

void TimelinePanelComponent::toggleAutomationForTrack(synth::TrackId trackId) {
    if (doc_ == nullptr)
        return;
    const auto* t = doc_->getTrack(trackId);
    if (t == nullptr || t->lanes.empty())
        return; // no-op: the button is hidden in this case anyway (see refreshFromDoc())

    // Already open on one of THIS track's lanes -> close. Anything else (closed, or open on a
    // different track) -> open this track's first lane, switching the strip if needed.
    const bool openOnThisTrack = automationStripVisible_ && doc_->getTrackForLane(selectedAutomationLane_) == t;
    if (openOnThisTrack)
        closeAutomationStrip();
    else
        showAutomationLane(t->lanes.front().id);
}

void TimelinePanelComponent::layoutTrackHeaders() {
    // Themed with a literal fallback, same pattern as resized() above — and the SAME value
    // (token x vertical-zoom scale) synth::ui::TimelineClipLaneArea reads for its own row height,
    // so header rows and clip rows never drift apart.
    const int rowHeight = currentRowHeight();

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
    saved = juce::jlimit((int)TimelineViewState::Snap::Off, (int)TimelineViewState::Snap::HundredTwentyEighth, saved);
    // setSnap (not a bare assignment) so a restored musical division is also where cycleSnapValue's
    // from-Off rule resumes — a restored Snap::Off leaves lastMusicalSnap at its default, which is
    // exactly the documented fallback.
    viewState_.setSnap((TimelineViewState::Snap)saved);
    snapCombo_.setSelectedId(saved + 1, juce::dontSendNotification);
    viewState_.snapEnabled =
        appProperties_->getUserSettings()->getBoolValue(kTimelineSnapEnabledPropertyKey, viewState_.snapEnabled);
    snapToggleButton_.setToggleState(viewState_.snapEnabled, juce::dontSendNotification);

    followPlayhead_ =
        appProperties_->getUserSettings()->getBoolValue(kTimelineFollowPlayheadPropertyKey, followPlayhead_);
    followPlayheadButton_.setToggleState(followPlayhead_, juce::dontSendNotification);
    pianoRoll_.setFollowPlayhead(followPlayhead_);

    // A pure forward — the transport bar owns and persists its own two keys. See this
    // method's header comment.
    transportBar_.setApplicationProperties(props);

    // Scale-panel visibility + user scales are the ROLL's own PropertiesFile-backed state (see
    // PianoRollComponent::setPropertiesFile); key-labels and note-colour overrides are read here.
    pianoRoll_.setPropertiesFile(appProperties_->getUserSettings());
    reloadPianoRollAppearancePrefs();
}

void TimelinePanelComponent::reloadPianoRollAppearancePrefs() {
    if (appProperties_ == nullptr || appProperties_->getUserSettings() == nullptr)
        return;
    auto& settings = *appProperties_->getUserSettings();

    const auto keyLabels = settings.getValue(kPianoRollKeyLabelsPropertyKey, "all");
    pianoRoll_.setKeyLabelMode(keyLabels.equalsIgnoreCase("c")
                                   ? synth::ui::PianoRollComponent::KeyLabelMode::OctavesOnly
                                   : synth::ui::PianoRollComponent::KeyLabelMode::AllNotes);
    pianoRoll_.setNoteColourOverrides(synth::ui::loadNoteColourOverrides(settings));
}

bool TimelinePanelComponent::setSnapValue(TimelineViewState::Snap value) {
    const bool changed = viewState_.snap != value;
    viewState_.setSnap(value);
    // The combo REFLECTS the view state; it never owns it. dontSendNotification so a programmatic
    // set can't re-enter onChange (which would call straight back into here).
    snapCombo_.setSelectedId((int)value + 1, juce::dontSendNotification);
    // Picking a division is an explicit "snap to THIS" — flip the master switch back on so the
    // choice takes effect immediately (choosing Snap::Off already means "no grid"). This is also
    // the single persist + ruler/grid repaint path; see setSnapEnabled below.
    setSnapEnabled(true);
    // The roll's gridlines and its new-note length both come from the division — a snap change is
    // the only thing that moves them, so this is where they are redrawn.
    pianoRoll_.repaint();
    return changed;
}

bool TimelinePanelComponent::cycleSnapValue(int direction) {
    using Snap = TimelineViewState::Snap;
    if (direction == 0)
        return false;

    // From Off there is no position for "one step finer" to be relative to, so both directions
    // re-enter at the last division the user actually chose (Bar if there wasn't one). See the
    // header for why this beats picking an end.
    if (viewState_.snap == Snap::Off) {
        const Snap entry = viewState_.lastMusicalSnap != Snap::Off ? viewState_.lastMusicalSnap : Snap::Bar;
        return setSnapValue(entry);
    }

    // Snap is declared coarsest -> finest (see TimelineViewState), so the step is a clamped +-1 on
    // the enum's own int. CLAMPED, never wrapped: parking on 1/128 under a held key is far less
    // surprising than silently landing back on Bar.
    const int stepped =
        juce::jlimit((int)Snap::Bar, (int)Snap::HundredTwentyEighth, (int)viewState_.snap + (direction > 0 ? 1 : -1));
    return setSnapValue((Snap)stepped);
}

void TimelinePanelComponent::setSnapEnabled(bool enabled) {
    viewState_.snapEnabled = enabled;
    persistSnapChoice();
    snapToggleButton_.setToggleState(enabled, juce::dontSendNotification);
    ruler_.repaint();
    repaint();
}

void TimelinePanelComponent::persistSnapChoice() {
    if (appProperties_ == nullptr || appProperties_->getUserSettings() == nullptr)
        return;
    appProperties_->getUserSettings()->setValue(kTimelineSnapPropertyKey, (int)viewState_.snap);
    appProperties_->getUserSettings()->setValue(kTimelineSnapEnabledPropertyKey, viewState_.snapEnabled);
    appProperties_->saveIfNeeded();
}

void TimelinePanelComponent::setFollowPlayheadEnabled(bool enabled) {
    followPlayhead_ = enabled;
    followPlayheadButton_.setToggleState(enabled, juce::dontSendNotification);
    pianoRoll_.setFollowPlayhead(enabled);
    persistFollowPlayheadChoice();
}

void TimelinePanelComponent::persistFollowPlayheadChoice() {
    if (appProperties_ == nullptr || appProperties_->getUserSettings() == nullptr)
        return;
    appProperties_->getUserSettings()->setValue(kTimelineFollowPlayheadPropertyKey, followPlayhead_);
    appProperties_->saveIfNeeded();
}

void TimelinePanelComponent::setScrollInverted(bool inverted) noexcept {
    scrollInverted_ = inverted;
    // Keep the roll in step — see this setter's header comment. It runs its OWN plain-scroll
    // branches (PianoRollComponent::mouseWheelMove), so a preference set on the panel chrome must
    // reach it directly rather than through anything shared like TimelineViewState.
    pianoRoll_.setScrollInverted(inverted);
}

void TimelinePanelComponent::setZoomScrollInverted(bool inverted) noexcept {
    zoomScrollInverted_ = inverted;
    pianoRoll_.setZoomScrollInverted(inverted); // same forwarding reason as setScrollInverted above
}

//==============================================================================
void TimelinePanelComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    // Reproject into the ruler's coordinate space regardless of whether the event originated on
    // this component or bubbled up from the ruler child — both share the same x == 0 origin as
    // TimelineViewState (the lanes/ruler content start), so this is exactly the anchor
    // beatToX/xToBeat expect.
    const double anchorX = (double)e.getEventRelativeTo(&ruler_).position.x;

    // Cubase-style bindings: Cmd = horizontal zoom, Cmd+Shift = vertical zoom (row height),
    // Shift or a trackpad's own deltaX = horizontal scroll, plain vertical wheel = vertical
    // track scroll (headers + lanes together).
    //
    // The two ZOOM branches are decided by their MODIFIERS, so they must not read a single axis:
    // macOS folds Shift+wheel into deltaX, which would leave Cmd+Shift+wheel reading deltaY == 0
    // and doing nothing at all. dominantWheelDelta() is that guard (see ScrollPolicy.h).
    //
    // Direction is read from the PHYSICAL gesture (wheelGestureIsUpward, isReversed-aware), not
    // from the delta's raw sign: "up zooms in" must mean the same finger motion whether or not the
    // OS has natural scrolling on, exactly the reasoning ScrollPolicy.h documents for that helper.
    // zoomScrollInverted_ XORs on top, the same second-deliberate-flip idiom scrollInverted_ already
    // applies to plain scrolling below. Magnitude is the dominant axis's unsigned size, so today's
    // exponential sensitivity curve is unchanged — only the sign moved from "the delta" to "the
    // gesture direction, then the preference".
    const bool zoomingIn = wheelGestureIsUpward(wheel) != zoomScrollInverted_;
    const double zoomMagnitude = std::abs((double)dominantWheelDelta(wheel)) * kZoomWheelSensitivity;
    const double zoomFactor = std::exp(zoomingIn ? zoomMagnitude : -zoomMagnitude);

    if (e.mods.isCommandDown() && e.mods.isShiftDown()) {
        zoomTrackRows(zoomFactor, (double)e.getEventRelativeTo(&clipLaneArea_).position.y);
        return;
    }

    if (e.mods.isCommandDown()) {
        zoomHorizontalAroundX(zoomFactor, anchorX);
        return;
    }

    // Plain scroll. scrollAmount() converts a wheel delta into the amount to ADD to a view origin
    // with juce::Viewport's sign convention (natural), plus this panel's inversion preference —
    // and both of these origins DO grow the Viewport way (firstVisibleBeat is the beat at x == 0,
    // trackScrollY the pixels scrolled off the top), so no extra axis mapping is needed here.
    const bool horizontal = e.mods.isShiftDown() || std::abs(wheel.deltaX) > std::abs(wheel.deltaY);
    if (horizontal) {
        // Which axis the gesture arrived on, NOT "the dominant delta": a Shift+wheel that the OS
        // left on deltaY and a trackpad's own sideways deltaX are both horizontal scrolls, and
        // either one is the amount to move by.
        const float delta = std::abs(wheel.deltaX) > std::abs(wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
        const double deltaPx = (double)scrollAmount(delta, scrollInverted_) * kScrollPixelsPerWheelUnit;
        viewState_.scrollBeats(deltaPx / viewState_.pixelsPerBeat);
        ruler_.repaint();
        repaint();
        return;
    }

    scrollTrackRows((double)scrollAmount(wheel.deltaY, scrollInverted_) * kScrollPixelsPerWheelUnit);
}

void TimelinePanelComponent::mouseMagnify(const juce::MouseEvent& e, float scaleFactor) {
    // Trackpad pinch: deliberate enough that it needs no modifier. Plain pinch = horizontal zoom
    // around the pinch point; Shift+pinch = vertical (row height) zoom.
    if (e.mods.isShiftDown()) {
        if (!std::isfinite(scaleFactor) || scaleFactor <= 0.0f)
            return;
        zoomTrackRows((double)scaleFactor, (double)e.getEventRelativeTo(&clipLaneArea_).position.y);
        return;
    }
    zoomHorizontalAroundX((double)scaleFactor, (double)e.getEventRelativeTo(&ruler_).position.x);
}

//==============================================================================
void TimelinePanelComponent::zoomHorizontalAroundX(double factor, double anchorX) {
    if (!std::isfinite(factor) || factor <= 0.0)
        return;
    viewState_.zoomAroundX(factor, anchorX);
    ruler_.repaint();
    repaint();
}

double TimelinePanelComponent::visibleCentreXInRuler() const noexcept {
    // The ruler's local x == 0 IS TimelineViewState's origin (see resized()), so its half-width is
    // the beat-space centre of what is on screen — the same anchor a wheel zoom would pass if the
    // pointer happened to sit in the middle of the strip.
    return (double)ruler_.getWidth() * 0.5;
}

double TimelinePanelComponent::visibleCentreYInLanes() const noexcept {
    // clipLaneArea_ fills gridLanesBounds_ exactly, and zoomTrackRows' anchor is in that
    // component's coordinates — so the middle visible row is simply half its height.
    return (double)gridLanesBounds_.getHeight() * 0.5;
}

void TimelinePanelComponent::zoomTimelineHorizontal(double factor) {
    zoomHorizontalAroundX(factor, visibleCentreXInRuler());
}

void TimelinePanelComponent::zoomTimelineVertical(double factor) {
    if (!std::isfinite(factor) || factor <= 0.0)
        return;
    zoomTrackRows(factor, visibleCentreYInLanes());
}

int TimelinePanelComponent::currentRowHeight() const {
    int base = TimelineTrackHeaderComponent::kRowHeight;
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel()))
        base = lf->getTheme().metrics.timelineTrackRowHeight;
    return std::max(8, (int)std::llround((double)base * viewState_.rowHeightScale));
}

double TimelinePanelComponent::maxTrackScrollPx() const {
    const int rows = doc_ != nullptr ? (int)doc_->getTracks().size() : 0;
    return std::max(0.0, (double)(rows * currentRowHeight() - gridLanesBounds_.getHeight()));
}

void TimelinePanelComponent::scrollTrackRows(double deltaPx) {
    const double before = viewState_.trackScrollY;
    viewState_.scrollTracksPx(deltaPx, maxTrackScrollPx());
    if (viewState_.trackScrollY == before)
        return;
    syncTrackScroll();
}

void TimelinePanelComponent::zoomTrackRows(double factor, double anchorLaneY) {
    // Keep the row under the pointer put: contentY scales with the row height, the visible y must
    // not move. Recompute from the actual (rounded, clamped) row heights rather than `factor` so
    // the clamps can't make the anchor drift.
    const int oldRowHeight = currentRowHeight();
    const double contentY = anchorLaneY + viewState_.trackScrollY;
    viewState_.scaleRowHeight(factor);
    const int newRowHeight = currentRowHeight();
    if (newRowHeight == oldRowHeight)
        return;
    viewState_.trackScrollY = contentY * (double)newRowHeight / (double)oldRowHeight - anchorLaneY;
    viewState_.scrollTracksPx(0.0, maxTrackScrollPx()); // clamp into the new range
    layoutTrackHeaders();
    syncTrackScroll();
}

void TimelinePanelComponent::syncTrackScroll() {
    // One writer, two readers: the header viewport mirrors trackScrollY, the lanes/roll repaint.
    // setViewPosition fires visibleAreaChanged, whose handler sees an unchanged value and stops —
    // no feedback loop.
    trackHeaderViewport_.setViewPosition(0, (int)std::llround(viewState_.trackScrollY));
    clipLaneArea_.repaint();
    repaint(gridLanesBounds_);
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
    // The piano roll's rect PLUS the ruler strip above it, expressed in the OVERLAY's coordinates.
    // While the roll is open the overlay skips these rows — the roll draws its own line
    // (LocalPlayheadClient), and the ruler strip is skipped too because it then labels bars
    // through the ROLL's mapping (setMappingOverride), where the overlay's shared-mapping x would
    // be a lie. While the roll is closed the region is ignored and the overlay owns its whole rect.
    playhead_.setLocalPlayheadRegion(ruler_.getBounds().getUnion(gridLanesBounds_) -
                                     playhead_.getBounds().getPosition());

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
    snapToggleButton_.setBounds(transportBar.removeFromRight(kSnapToggleButtonWidth).reduced(2));
    // Follow-playhead sits immediately left of the snap toggle — see its member comment.
    followPlayheadButton_.setBounds(transportBar.removeFromRight(kFollowPlayheadButtonWidth).reduced(2));
    // The tool strip sits immediately left of the snap controls: both are "how the next edit
    // behaves" chrome, so they read as one group, and neither pushes the transport controls off
    // their left-aligned home. Laid out left-to-right in EditTool order (1, 3, 4, 5, 7, 8).
    auto toolStrip = transportBar.removeFromRight(kEditToolButtonWidth * (int)kAllEditTools.size());
    for (auto tool : kAllEditTools)
        if (auto* button = getToolButton(tool))
            button->setBounds(toolStrip.removeFromLeft(kEditToolButtonWidth).reduced(2));
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

    // Follow-playhead toggle: an always-visible pill outline, drawn HERE in the PARENT's paint()
    // (which always runs before the child's — same ordering the grid/clip-lane-area comment above
    // relies on) rather than trusting either the button's own resting-state fill or its icon
    // having loaded. AppLookAndFeel::drawDrawableButton's off/non-hovered fill is deliberately
    // transparent for a plain DrawableButton, and applyToolStripTheme() may not have found a
    // themed LookAndFeel by the time it first ran (see parentHierarchyChanged() above) — or this
    // build may simply have no icon asset linked at all. Either way the button painted NOTHING at
    // rest: present in the tree and clickable, but genuinely invisible — the reported bug.
    // snapToggleButton_ never has this problem because a plain TextButton always gets
    // AppLookAndFeel::drawButtonBackground's pill+border fill; this gives the one DrawableButton
    // on the strip with no text label to fall back on that same always-on affordance.
    if (const auto followBounds = followPlayheadButton_.getBounds().toFloat(); !followBounds.isEmpty()) {
        g.setColour(border.withAlpha(0.35f));
        g.fillRoundedRectangle(followBounds, 3.0f);
        g.setColour(border.withAlpha(0.7f));
        g.drawRoundedRectangle(followBounds.reduced(0.5f), 3.0f, 1.0f);
    }

    // Bar/beat/subdivision grid across the lanes region (below the ruler), using the SAME shared
    // TimelineViewState the ruler paints from. Colours come from the shared three-level policy in
    // TimelineClipLaneArea.h (gridLineColourFor / gridLevelIsReadable) — the piano roll paints its
    // grid from the same policy, so the two surfaces can't drift apart in visibility. The policy
    // lifts `border` toward the background's contrasting colour before applying the per-level
    // alpha, because on dark themes `border` alone sits too close to bg0 to read at any alpha.
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

        // The current snap division paints as a third, lightest level BETWEEN the beat lines —
        // only when it is finer than a beat and wide enough on screen to read as a grid rather
        // than as noise (gridLevelIsReadable's ~3 px density guard, the same rule Cubase applies).
        // Requiring drawBeatLines keeps the hierarchy monotonic: a subdivision may never be
        // visible while its parent beat level is hidden (possible otherwise, because the beat
        // gate is ~8 px/beat while the readability guard is ~3 px/line).
        const double division = viewState_.divisionBeats(beatsPerBar);
        const bool drawSubdivisionLines = drawBeatLines && division > 0.0 && division < 1.0 &&
                                          synth::ui::gridLevelIsReadable(division, viewState_.pixelsPerBeat);

        const auto barColour = synth::ui::gridLineColourFor(synth::ui::GridLineLevel::Bar, border, bg);
        const auto beatColour = synth::ui::gridLineColourFor(synth::ui::GridLineLevel::Beat, border, bg);
        const auto subColour = synth::ui::gridLineColourFor(synth::ui::GridLineLevel::Subdivision, border, bg);

        for (juce::int64 bar = firstBar; bar <= lastBar; ++bar) {
            const double barBeat = (double)bar * beatsPerBar;
            const double x = viewState_.beatToX(barBeat);
            // Cull only the BAR LINE itself when it sits outside the view — never the whole bar.
            // A bar whose own line has scrolled off the left edge still owns beats and
            // subdivisions that ARE on screen; a whole-bar `continue` here is exactly the bug
            // where every grid line left of the first visible bar line vanished while scrolling.
            // The beat/subdivision loops below cull per line.
            if (x >= -1.0 && x <= widthPx + 1.0) {
                g.setColour(barColour);
                g.drawVerticalLine(xOrigin + (int)std::llround(x), (float)top, (float)bottom);
            }

            if (drawBeatLines) {
                for (int beatInBar = 1; beatInBar < beatsPerBarRounded; ++beatInBar) {
                    const double beatX = viewState_.beatToX(barBeat + (double)beatInBar);
                    if (beatX < 0.0 || beatX > widthPx)
                        continue;
                    g.setColour(beatColour);
                    g.drawVerticalLine(xOrigin + (int)std::llround(beatX), (float)top, (float)bottom);
                }
            }

            if (drawSubdivisionLines) {
                g.setColour(subColour);
                for (double beatInBar = division; beatInBar < beatsPerBar - division * 0.5; beatInBar += division) {
                    // Skip positions that already got a bar/beat line — a subdivision line under
                    // a stronger one would just anti-alias the stronger line's edge.
                    if (std::abs(beatInBar - (double)std::llround(beatInBar)) < division * 0.25)
                        continue;
                    const double subX = viewState_.beatToX(barBeat + beatInBar);
                    if (subX < 0.0 || subX > widthPx)
                        continue;
                    g.drawVerticalLine(xOrigin + (int)std::llround(subX), (float)top, (float)bottom);
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
