// TrackLaneHeaderComponent.cpp
//
// Concern: one expanded automation lane row's header -- label, orphan state, record mode, remove
// (docs/timeline/track-automation.md#lane-rows).

#include "TrackLaneHeaderComponent.h"

#include "AppUndoManager.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/TimelineTrackHeaderComponent.h"
#include "UI/Timeline/TrackColour.h"

namespace synth::ui {

namespace {
// Lane rows sit indented under their track: the stripe lines up with the track header's own
// colour swatch column, and the gap to its left reads as "child of the row above".
constexpr int kIndentPx = 14;
constexpr int kStripeWidth = 3;
constexpr int kRecordModeComboWidth = 62;
constexpr int kPadding = 3;

struct LaneColours {
    juce::Colour row{0xff161A20};
    juce::Colour rowFocused{0xff1D232B};
    juce::Colour border{0xff2A2F38};
    juce::Colour text{0xffEAEEF3};
    juce::Colour textMuted{0xff8A93A0};
    juce::Colour warning{0xffE0A33D};
    juce::Colour accent{0xff00D1FF};
};

// A lane row is visibly a DIFFERENT kind of row from a track header: a step darker (halfway from
// the header surface down to bg1) instead of a new theme token nobody could re-skin.
LaneColours laneColoursFor(const juce::Component& c) {
    LaneColours colours;
    if (auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&c.getLookAndFeel())) {
        const auto& t = lf->getTheme().colors;
        colours.row = t.surface.interpolatedWith(t.bg1, 0.5f);
        colours.rowFocused = t.surface.interpolatedWith(t.accent, 0.08f);
        colours.border = t.border;
        colours.text = t.textPrimary;
        colours.textMuted = t.textMuted;
        colours.warning = t.warning;
        colours.accent = t.accent;
    }
    return colours;
}
} // namespace

// The SAME label the bottom strip's lane picker shows (TimelinePanelComponent::
// collectAutomationLaneOptions calls this too): the node's display name, falling back to the uuid's
// first 8 characters, then the parameter's display name, falling back to the raw paramId (a hosted
// plugin's paramIds are opaque). Joined with a middle dot, spelled through fromUTF8 so the literal
// stays ASCII (Source/CLAUDE.md).
juce::String automationLaneLabel(TrackHeaderHost* host, const synth::AutomationLane& lane) {
    juce::String nodeLabel = host != nullptr ? host->getNodeDisplayName(lane.nodeUuid) : juce::String();
    if (nodeLabel.isEmpty())
        nodeLabel = lane.nodeUuid.substring(0, 8);
    juce::String paramLabel =
        host != nullptr ? host->getParameterDisplayName(lane.nodeUuid, lane.paramId) : juce::String();
    if (paramLabel.isEmpty())
        paramLabel = lane.paramId;
    return nodeLabel + juce::String::fromUTF8(" \xC2\xB7 ") + paramLabel;
}

TrackLaneHeaderComponent::TrackLaneHeaderComponent() {
    setComponentID("trackLaneHeader");
    addAndMakeVisible(recordModeCombo_);
    recordModeCombo_.setComponentID("trackLaneRecordModeCombo");
    recordModeCombo_.addItem("Off", 1);
    recordModeCombo_.addItem("Read", 2);
    recordModeCombo_.addItem("Touch", 3);
    recordModeCombo_.addItem("Latch", 4);
    recordModeCombo_.addItem("Write", 5);
    recordModeCombo_.setTooltip("Automation record mode for this lane");
    // Chrome never steals focus on click (MainComponent::resolveEditSurface reads real focus) --
    // the same opt-out the strip's own record-mode combo makes.
    recordModeCombo_.setMouseClickGrabsKeyboardFocus(false);
    recordModeCombo_.onChange = [this] { applyRecordModeChoice(recordModeCombo_.getSelectedId()); };
}

void TrackLaneHeaderComponent::bind(synth::TimelineDoc* doc, AppUndoManager* undoManager, TrackHeaderHost* host,
                                    synth::LaneId lane) {
    doc_ = doc;
    undoManager_ = undoManager;
    host_ = host;
    lane_ = lane;
    refreshFromDoc();
}

void TrackLaneHeaderComponent::refreshFromDoc() {
    const auto* lane = doc_ != nullptr ? doc_->getLane(lane_) : nullptr;
    if (lane == nullptr) {
        label_ = {};
        orphaned_ = false;
        repaint();
        return;
    }
    label_ = automationLaneLabel(host_, *lane);
    orphaned_ = lane->orphaned;
    setTooltip(orphaned_ ? label_ + " - the module or parameter this lane automated is gone" : label_);
    recordModeCombo_.setSelectedId(lane->recordMode + 1, juce::dontSendNotification);

    if (const auto* track = doc_->getTrackForLane(lane_)) {
        const auto& tracks = doc_->getTracks();
        const auto index = (int)(track - tracks.data());
        trackColour_ = resolveTrackColour(track->colourArgb, index, track->muted);
    }
    repaint();
}

void TrackLaneHeaderComponent::setFocusedLane(bool focused) {
    if (focused == focused_)
        return;
    focused_ = focused;
    repaint();
}

// A manual selector change IS a user gesture, so it records an undo step -- identical to the
// bottom strip's record-mode combo (TimelinePanelComponent::applyAutomationRecordModeChoice), unlike
// AutomationRecorder's programmatic Write-drops-to-Touch-on-stop call.
void TrackLaneHeaderComponent::applyRecordModeChoice(int comboId) {
    if (doc_ == nullptr || doc_->getLane(lane_) == nullptr || comboId < 1)
        return;
    const auto lane = lane_;
    const int mode = comboId - 1;
    runEdit([this, lane, mode] { doc_->setLaneRecordMode(lane, mode); });
}

void TrackLaneHeaderComponent::applyContextMenuChoice(int menuId) {
    if (menuId != kRemoveLaneMenuId || doc_ == nullptr || doc_->getLane(lane_) == nullptr)
        return;
    const auto lane = lane_;
    runEdit([this, lane] { doc_->removeLane(lane); });
}

juce::PopupMenu TrackLaneHeaderComponent::buildContextMenu() const {
    juce::PopupMenu menu;
    menu.addItem(kRemoveLaneMenuId, "Remove lane", doc_ != nullptr && doc_->getLane(lane_) != nullptr);
    return menu;
}

void TrackLaneHeaderComponent::runEdit(const std::function<void()>& mutation) {
    if (undoManager_ != nullptr)
        undoManager_->recordTimelineChange(*doc_, mutation);
    else
        mutation();
}

void TrackLaneHeaderComponent::paint(juce::Graphics& g) {
    const auto colours = laneColoursFor(*this);
    g.fillAll(focused_ ? colours.rowFocused : colours.row);

    g.setColour(trackColour_.withMultipliedAlpha(0.35f));
    g.fillRect(0, 0, 3, getHeight()); // the track's own tinted left edge continues down its lanes
    g.setColour(trackColour_);
    g.fillRect(kIndentPx - kStripeWidth - 3, 3, kStripeWidth, getHeight() - 6);

    g.setColour(colours.border.withAlpha(0.6f));
    g.drawHorizontalLine(getHeight() - 1, (float)kIndentPx, (float)getWidth());

    auto text = getLocalBounds().withTrimmedLeft(kIndentPx).withTrimmedRight(kRecordModeComboWidth + kPadding * 2);
    g.setColour(orphaned_ ? colours.warning
                          : (focused_ ? colours.text : colours.textMuted.interpolatedWith(colours.text, 0.5f)));
    g.setFont(juce::FontOptions(12.0f));
    g.drawFittedText(label_, text.reduced(2, 0), juce::Justification::centredLeft, 1, 0.85f);

    if (focused_) {
        g.setColour(colours.accent);
        g.drawRect(getLocalBounds().withTrimmedLeft(kIndentPx - kStripeWidth - 4), 1);
    }
}

void TrackLaneHeaderComponent::resized() {
    auto bounds = getLocalBounds().reduced(kPadding);
    const int comboHeight = juce::jmin(bounds.getHeight(), 22);
    recordModeCombo_.setBounds(
        bounds.removeFromRight(kRecordModeComboWidth).withSizeKeepingCentre(kRecordModeComboWidth, comboHeight));
}

void TrackLaneHeaderComponent::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) {
        auto menu = buildContextMenu();
        if (menuHook_) {
            menuHook_(menu);
            return;
        }
        juce::Component::SafePointer<TrackLaneHeaderComponent> safeThis(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safeThis](int result) {
            if (auto* self = safeThis.getComponent())
                self->applyContextMenuChoice(result);
        });
        return;
    }
    if (onFocusRequested)
        onFocusRequested(lane_);
}

} // namespace synth::ui
