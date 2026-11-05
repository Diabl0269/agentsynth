// DrumMachineComponent: what a screen reader says. The grid is one group whose value is the focused cell, so arrowing
// speaks "Kick, step 5, hit, loudness 100, accent" or "Kick, step 5, empty".

#include "DrumMachineComponent.h"
#include "UI/Layout/EditorTabWalk.h"

#include "TestMode/TestModeStateSource.h"
#include "UI/ModuleViews/ModuleViewAccessibility.h"
#include <algorithm>
#include <map>

namespace synth::ui {

// The cell's per-hit settings that are not their default, as ", chance 50%, repeat 3, nudge -10%, condition 2/4".
juce::String DrumMachineComponent::describeHitSettings(int pitch, int step) const {
    const auto it = cells_.find(drumgrid::cellKey(pitch, step));
    if (it == cells_.end())
        return {};
    juce::String text;
    for (int i = 1; i < kDrumLaneKindCount; ++i) {
        const auto& spec = drumLaneSpec(static_cast<DrumLaneKind>(i));
        const int value = drumLaneValueOf(spec.kind, it->second);
        if (value != drumLaneValueOf(spec.kind, drumgrid::CellHits{}))
            text += ", " + spec.word + " " + spec.textOf(value);
    }
    return text;
}

juce::String DrumMachineComponent::getAccessibilityValueText() const {
    const int pitch = getLanePitch(focus_.row);
    if (!clipId_.isValid() || pitch < 0)
        return "No drum rows";
    auto text = slots_[(size_t)focus_.row].lane.name + ", step " + juce::String(focus_.step + 1);
    // A cell past the drum's end repeats an earlier one: it reads that cell's hit, after saying so.
    const int source = ghostSourceStep(focus_.row, focus_.step);
    if (source >= 0)
        text += ", repeats step " + juce::String(source + 1);
    const int step = source >= 0 ? source : focus_.step;
    const auto stored = cells_.find(drumgrid::cellKey(pitch, step));
    const int velocity =
        source >= 0 ? (stored == cells_.end() ? 0 : stored->second.velocity) : getCellVelocity(focus_.row, focus_.step);
    if (velocity == 0)
        return text + ", empty";
    auto described = text + ", hit, loudness " + juce::String(velocity);
    if (drumgrid::isAccent(velocity))
        described += ", accent";
    described += describeHitSettings(pitch, step);
    if (rowSilenced(pitch))
        described += ", row silenced";
    return described;
}

std::unique_ptr<juce::AccessibilityHandler> DrumMachineComponent::createAccessibilityHandler() {
    return makeValueTextHandler(*this, [this] { return getAccessibilityValueText(); });
}

void DrumMachineComponent::focusGained(FocusChangeType) {
    repaint();
    announceFocus();
}

// Focus leaving the grid (to a header stop, or anywhere else) starts the Tab walk over. The walk is null while the
// component is torn down.
void DrumMachineComponent::focusLost(FocusChangeType) {
    if (tabWalk_ != nullptr)
        tabWalk_->reset();
    repaint();
}

// The header row in screen order. Rows, lane chips and lanes are not on the walk: the grid's own keys reach a row
// (Alt+Left/Right, the context-menu key), and a Tab after Ideas comes back to the grid.
void DrumMachineComponent::setupTabWalk() {
    tabWalk_->setShortcuts([this] { return shortcuts_; });
    tabWalk_->setStops([this] {
        return std::vector<juce::Component*>{&viewSwitch_, &playClipButton_, &showAllButton_, &stepsCombo_,
                                             &swingField_, &humaniseKnob_,   &ideasButton_};
    });
}

// The accessible name of the control that holds focus inside the drum machine: its title, else a button's text.
static juce::String focusedNameIn(const juce::Component& owner) {
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    if (focused == nullptr || (focused != &owner && !owner.isParentOf(focused)))
        return {};
    if (focused->getTitle().isNotEmpty())
        return focused->getTitle();
    if (auto* button = dynamic_cast<juce::Button*>(focused))
        return button->getButtonText();
    return {};
}

// Test mode: a hidden child (id `drumMachineState`) reports `hitCount` (cells holding a hit), `hits` (each row's steps
// as `pitch:step,step;...`), `stroke` (a paint or erase stroke is in flight) and `focused` (the name of the
// focused control inside the editor, empty when focus is elsewhere).
class DrumMachineComponent::StateProbe
    : public juce::Component
    , public synth::testmode::StateSource {
public:
    explicit StateProbe(const DrumMachineComponent& owner)
        : owner_(owner) {
        setComponentID("drumMachineState");
        setInterceptsMouseClicks(false, false);
        setAccessible(false);
    }
    void addTestModeState(juce::DynamicObject& into) const override {
        std::map<int, std::vector<int>> byPitch;
        for (const auto& [key, hits] : owner_.cells_)
            byPitch[static_cast<int>(key / 1000000)].push_back(static_cast<int>(key % 1000000));
        juce::StringArray rows;
        for (auto& [pitch, steps] : byPitch) {
            std::sort(steps.begin(), steps.end());
            juce::StringArray list;
            for (const int step : steps)
                list.add(juce::String(step));
            rows.add(juce::String(pitch) + ":" + list.joinIntoString(","));
        }
        into.setProperty("hitCount", static_cast<int>(owner_.cells_.size()));
        into.setProperty("hits", rows.joinIntoString(";"));
        into.setProperty("stroke", owner_.gesture_.kind == Gesture::Kind::Stroke);
        into.setProperty("focused", focusedNameIn(owner_)); // the Tab walk's stop, read as a screen reader names it
    }

private:
    const DrumMachineComponent& owner_;
};

void DrumMachineComponent::attachStateProbe() {
    stateProbe_ = std::make_unique<StateProbe>(*this);
    addChildComponent(*stateProbe_);
    stateProbe_->setBounds(0, 0, 1, 1);
}

} // namespace synth::ui
