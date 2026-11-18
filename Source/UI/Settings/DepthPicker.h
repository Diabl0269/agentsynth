#pragma once

#include "TestMode/TestModeStateSource.h"
#include "UI/Graph/CardWidgets/CardSegmentedSwitch.h"
#include "UI/Theme/DepthLevel.h"
#include "UI/Theme/Theme.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

// Settings > Appearance > Controls > Depth: a "Depth" caption beside a three-segment switch (Flat | Soft | Rich), the
// same CardSegmentedSwitch the cards use. One Tab stop; Left/Right (and Home/End) pick as they move, a click picks.
// To a screen reader it is a group named "Depth" holding one radio button per level, the picked one checked.
class DepthPicker
    : public juce::Component
    , public synth::testmode::StateSource {
public:
    DepthPicker();
    ~DepthPicker() override;

    // Fired after every change by the user; the owner applies and persists it.
    std::function<void(synth::theme::DepthLevel)> onChanged;

    // Message thread only. Does not fire onChanged.
    void setDepthLevel(synth::theme::DepthLevel level);
    synth::theme::DepthLevel getDepthLevel() const noexcept { return level_; }
    // The theme the caption paints with (the owner passes the active theme).
    void setTheme(const synth::theme::Theme& theme);

    static constexpr int kPreferredHeight = 28;

    void resized() override;
    void addTestModeState(juce::DynamicObject& into) const override;

    CardSegmentedSwitch& getSwitchForTest() { return *switch_; }

private:
    synth::theme::DepthLevel level_{synth::theme::DepthLevel::Soft};
    juce::Label caption_{{}, "Depth"};
    std::unique_ptr<CardSegmentedSwitch> switch_;
};

} // namespace synth::ui
