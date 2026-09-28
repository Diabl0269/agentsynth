// TrackAutomationLanesToolbar.cpp
//
// Concern: the timeline toolbar's automation controls (Draw-tool curve selector, global-strip
// toggle, automation-follows-clips toggle) and the edit tool -> lane editor tool mapping
// (docs/timeline/edit-tools.md#the-curve-selector).

#include "TrackAutomationLanes.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr int kCurveButtonWidth = 30;
constexpr int kGlobalButtonWidth = 42; // glyph plus a lane-count badge
constexpr int kFollowsButtonWidth = 30;
} // namespace

void TrackAutomationLanes::setUpToolbarButtons() {
    curveButton_.setComponentID("timelineCurveSelector");
    curveButton_.setGlyph([this](juce::Rectangle<float> area) { return laneCurveGlyph(curve_, area); });
    curveButton_.onClick = [this] { showCurveMenu(); };

    globalButton_.setComponentID("timelineGlobalAutomationButton");
    globalButton_.onClick = [this] {
        if (callbacks_.toggleGlobalStrip)
            callbacks_.toggleGlobalStrip();
    };

    followsButton_.setComponentID("timelineAutomationFollowsClipsButton");
    followsButton_.setTooltip("Automation follows clips: moving, copying or deleting a clip also moves, copies "
                              "or deletes the automation under it on its own track");
    followsButton_.onClick = [this] {
        if (callbacks_.toggleFollowsClips)
            callbacks_.toggleFollowsClips();
    };

    // Toolbar chrome never takes focus on click: MainComponent::resolveEditSurface reads real focus,
    // so a click here must not reroute the next Cmd+C/V from the clips to the graph (the same
    // opt-out every other timeline toolbar button makes).
    for (auto* button : {&curveButton_, &globalButton_, &followsButton_}) {
        button->setClickingTogglesState(false);
        button->setWantsKeyboardFocus(false);
        button->setMouseClickGrabsKeyboardFocus(false);
    }
    setCurve(curve_);
}

void TrackAutomationLanes::addToolbarTo(juce::Component& parent) {
    parent.addChildComponent(curveButton_);
    parent.addAndMakeVisible(globalButton_);
    parent.addAndMakeVisible(followsButton_);
    curveButton_.setVisible(editTool_ == EditTool::Draw);
}

// Right to left: follows-clips, global automation, then the curve selector -- which therefore
// sits immediately right of the edit-tool strip's Draw button, the tool it belongs to. Its slot is
// reserved even while hidden, so switching tools never shifts the buttons around it.
void TrackAutomationLanes::layoutToolbar(juce::Rectangle<int>& area) {
    followsButton_.setBounds(area.removeFromRight(kFollowsButtonWidth).reduced(2));
    globalButton_.setBounds(area.removeFromRight(kGlobalButtonWidth).reduced(2));
    curveButton_.setBounds(area.removeFromRight(kCurveButtonWidth).reduced(2));
}

void TrackAutomationLanes::refreshToolbar(int globalLaneCount, bool stripOpen, bool followsClips) {
    globalButton_.setToggleState(stripOpen, juce::dontSendNotification);
    globalButton_.setBadgeText(globalLaneCount > 0 ? juce::String(globalLaneCount) : juce::String());
    globalButton_.setTooltip(
        "Global automation (lanes not owned by one track): " +
        (globalLaneCount == 1 ? juce::String("1 lane") : juce::String(globalLaneCount) + " lanes") +
        (stripOpen ? ". Click to hide the strip." : ". Click to show the strip."));
    followsButton_.setToggleState(followsClips, juce::dontSendNotification);
}

//==============================================================================
// The ONE timeline tool also drives every lane row editor (docs/timeline/track-automation.md#tools):
// setActiveTool pushes it in here next to the clip lanes and the piano roll.
void TrackAutomationLanes::setEditTool(EditTool tool) {
    editTool_ = tool;
    curveButton_.setVisible(tool == EditTool::Draw && curveButton_.getParentComponent() != nullptr);
    applyToolToAllEditors();
}

void TrackAutomationLanes::setCurve(LaneCurve curve) {
    curve_ = curve;
    curveButton_.setTooltip(juce::String("Draw curve: ") + laneCurveName(curve) +
                            " (automation lanes under tracks; click to choose)");
    curveButton_.repaint();
    applyToolToAllEditors();
}

void TrackAutomationLanes::applyCurveMenuChoice(int menuId) {
    if (menuId < 1 || menuId > (int)kAllLaneCurves.size())
        return;
    setCurve(kAllLaneCurves[(std::size_t)(menuId - 1)]);
}

void TrackAutomationLanes::applyToolToEditor(AutomationLaneEditor& editor) const {
    const auto mapped = laneEditorToolFor(editTool_, curve_);
    editor.setTool(mapped.tool);
    if (mapped.tool == AutomationLaneEditor::Tool::Shape)
        editor.setShapeKind(mapped.shape);
}

void TrackAutomationLanes::applyToolToAllEditors() {
    for (auto& editor : editors_)
        applyToolToEditor(*editor);
}

// Each entry carries its own drawn glyph (a DrawablePath of the same path the button paints), so
// the menu reads as a palette of shapes rather than a list of words.
void TrackAutomationLanes::showCurveMenu() {
    juce::Colour ink = juce::Colours::white;
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&curveButton_.getLookAndFeel()))
        ink = lf->getTheme().colors.textPrimary;

    juce::PopupMenu menu;
    for (std::size_t i = 0; i < kAllLaneCurves.size(); ++i) {
        const auto curve = kAllLaneCurves[i];
        auto glyph = std::make_unique<juce::DrawablePath>();
        glyph->setPath(laneCurveGlyph(curve, {0.0f, 0.0f, 18.0f, 12.0f}));
        glyph->setFill(juce::FillType(juce::Colours::transparentBlack));
        glyph->setStrokeFill(juce::FillType(ink));
        glyph->setStrokeType(juce::PathStrokeType(1.4f));
        juce::PopupMenu::Item item(laneCurveName(curve));
        item.setID((int)i + 1).setTicked(curve == curve_).setImage(std::move(glyph));
        menu.addItem(std::move(item));
    }
    juce::Component::SafePointer<juce::Component> safeButton(&curveButton_);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&curveButton_), [this, safeButton](int result) {
        if (safeButton != nullptr && result != 0)
            applyCurveMenuChoice(result);
    });
}

} // namespace synth::ui
