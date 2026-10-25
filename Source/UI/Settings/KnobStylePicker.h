#pragma once

#include "UI/Theme/KnobStyle.h"
#include "UI/Theme/Theme.h"
#include <array>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

// Settings > Appearance > Controls: two rows of five clickable style previews, "Knobs" and "Faders", each row
// its own radio group, plus the "Colour controls by module family" switch. Knob previews are painted by
// synth::theme::paintKnob and fader previews by synth::theme::fader::paint, the painters the app's own
// controls use. Left/Right move between the previews of a row, Space/Return picks.
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

    static constexpr int kPreferredHeight = 168;

    void resized() override;

    juce::Button& getStyleButtonForTest(int index);      // knob row
    juce::Button& getFaderStyleButtonForTest(int index); // fader row
    juce::ToggleButton& getFamilyToggleForTest() { return familyToggle_; }

private:
    class StyleButton;
    enum class Kind { Knob, Fader };
    void styleChosen(Kind kind, int index);
    void refreshPreviewColour();

    synth::theme::KnobAppearance appearance_;
    synth::theme::Theme theme_;
    using Row = std::array<std::unique_ptr<StyleButton>, synth::theme::kKnobStyleCount>;
    Row buttons_;      // knob previews
    Row faderButtons_; // fader previews
    juce::Label knobRowLabel_{{}, "Knobs"};
    juce::Label faderRowLabel_{{}, "Faders"};
    juce::ToggleButton familyToggle_{"Colour controls by module family"};
};

} // namespace synth::ui
