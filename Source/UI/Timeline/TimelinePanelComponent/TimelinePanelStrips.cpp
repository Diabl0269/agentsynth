// TimelinePanelStrips.cpp
//
// The edit-tool strip (getToolButton/setActiveTool/applyToolStripTheme), the piano-roll
// open/close pair, and the automation strip (lane/record-mode selection, show/close).
// TimelinePanelComponent is declared in TimelinePanelComponent.h; sibling
// TimelinePanel*.cpp files in this directory hold the rest of the class.

#include "TimelinePanelComponent.h"

#include "AppUndoManager.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
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
} // namespace

//==============================================================================
// ---- Edit-tool strip ----

// The six buttons are built in the constructor, unconditionally (a headless build simply has no
// icon to draw in them). Exposed so a test can click one rather than synthesise a key press.
juce::DrawableButton* TimelinePanelComponent::getToolButton(EditTool tool) const noexcept {
    return toolButtons_[(std::size_t)tool].get();
}

// Owned here (rather than by the clip-lane area or the roll individually) because the two share a
// rect -- only one is ever visible -- and a tool row that changed meaning depending on which
// editor happened to be showing would be a trap. Setting it pushes the tool into BOTH editors and
// lights the matching strip button; the number keys (1/3/4/5/7/8) and the buttons are the two ways
// a user reaches it.
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

// The one thing this panel needs to redo on a theme switch (every other colour it uses is read at
// paint time through the same dynamic_cast) -- a theme switch re-tints every icon and can move the
// `toolActive` token, and both live in the LnF rather than in a per-button copy.
void TimelinePanelComponent::lookAndFeelChanged() { applyToolStripTheme(); }

// The complement to lookAndFeelChanged() above: re-applies the tool-strip icons whenever this
// component's ANCESTOR CHAIN changes, not just when its resolved LookAndFeel does. A themed
// LookAndFeel change (setLookAndFeel/sendLookAndFeelChange) only reaches components that are
// ALREADY attached as children at the moment it fires; the plugin editor calls
// setLookAndFeel(&processor.getLookAndFeel()) on itself BEFORE it adds its MainComponent (and this
// panel, several levels further down) as a child -- see AgentSynthPluginEditor's constructor -- so
// that notification never reaches an unattached TimelinePanelComponent, and its constructor-time
// applyToolStripTheme() call found no themed LookAndFeel on the ancestor chain yet either. When the
// panel IS attached moments later (addAndMakeVisible), JUCE fires parentHierarchyChanged() down the
// newly-added subtree -- not lookAndFeelChanged() -- so this is the one hook guaranteed to run at
// that point. Idempotent and cheap either way.
void TimelinePanelComponent::parentHierarchyChanged() { applyToolStripTheme(); }

//==============================================================================
void TimelinePanelComponent::openPianoRoll(synth::ClipId id) {
    pianoRoll_.openClip(id);
    if (!pianoRoll_.isOpen())
        return; // id didn't resolve to a live clip — PianoRollComponent::openClip's own contract

    clipLaneArea_.setVisible(false);
    pianoRoll_.setVisible(true);
    pianoRoll_.grabKeyboardFocus();
    // The lanes region is carved DIFFERENTLY once the roll is open — it reserves a toolbar row above
    // the ruler (see resized()) — and isOpen() is what that carve-up branches on, so the layout has
    // to be re-run now rather than waiting for the next resize.
    resized();
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
    resized(); // the toolbar row goes away and the ruler moves back to the top — same reason as above
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

// Existing lanes first (in track order then lane order), then "Add lane..." entries -- index i is
// menu id i + 1, the same convention TimelineTrackHeaderComponent::collectBindingOptions() uses.
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
            juce::String paramLabel = trackHeaderHost_ != nullptr
                                          ? trackHeaderHost_->getParameterDisplayName(lane.nodeUuid, lane.paramId)
                                          : juce::String();
            if (paramLabel.isEmpty())
                paramLabel = lane.paramId;
            options.push_back({lane.id, nodeLabel + juce::String::fromUTF8(" \xC2\xB7 ") + paramLabel, false, {}});
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

} // namespace synth::ui
