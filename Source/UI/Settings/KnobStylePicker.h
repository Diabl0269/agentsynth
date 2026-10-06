#pragma once

#include "UI/Theme/KnobStyle.h"
#include "UI/Theme/Theme.h"
#include <array>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

// Settings > Appearance > Controls: six clickable control-style previews (a style sets knobs and faders
// together) in one radio group plus the "Colour controls by module family" switch. Previews are painted by
// synth::theme::paintKnob, the same painter the app's knobs use. Left/Right move between the previews, Space/Return
// picks.
class KnobStylePicker : public juce::Component {
public:
    KnobStylePicker();
    ~KnobStylePicker() override;

    // Fired after every change by the user; the owner persists it.
    std::function<void(const synth::theme::KnobAppearance&)> onChanged;

    // Message thread only. Does not fire onChanged.
    void setAppearance(const synth::theme::KnobAppearance& appearance);
    const synth::theme::KnobAppearance& getAppearance() const noexcept { return appearance_; }
    // The theme the previews paint with (the owner passes the active theme).
    void setTheme(const synth::theme::Theme& theme);

    static constexpr int kPreferredHeight = 108;

    void resized() override;

    juce::Button& getStyleButtonForTest(int index);
    juce::ToggleButton& getFamilyToggleForTest() { return familyToggle_; }

private:
    class StyleButton;
    void styleChosen(int index);
    void refreshPreviewColour();

    synth::theme::KnobAppearance appearance_;
    synth::theme::Theme theme_;
    std::array<std::unique_ptr<StyleButton>, synth::theme::kKnobStyleCount> buttons_;
    juce::ToggleButton familyToggle_{"Colour controls by module family"};
};

} // namespace synth::ui
