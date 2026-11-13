#pragma once

#include "UI/Theme/KnobStyle.h"
#include "UI/Theme/Theme.h"
#include <array>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>

namespace synth::ui {

// Settings > Appearance > Controls: two rows of five clickable style previews, "Knobs" and "Faders", each row
// its own radio group, plus the "Colour controls by module family" switch. Knob previews are painted by
// synth::theme::paintKnob and fader previews by synth::theme::fader::paint, the painters the app's own
// controls use. Left/Right move between the previews of a row, Space/Return picks.
//
// In Mode::Override (a module's own style, ControlStylePanel) each row gains a leading "Follow" preview that
// means "no choice of its own", the family switch is hidden, and the picker reports std::optional<KnobStyle>
// per row instead of a KnobAppearance.
class KnobStylePicker : public juce::Component {
public:
    enum class Mode { Settings, Override };
    explicit KnobStylePicker(Mode mode = Mode::Settings);
    ~KnobStylePicker() override;

    // Fired after every change by the user; the owner persists it.
    std::function<void(const synth::theme::KnobAppearance&)> onChanged;

    // Message thread only. Does not fire onChanged.
    void setAppearance(const synth::theme::KnobAppearance& appearance);
    const synth::theme::KnobAppearance& getAppearance() const noexcept { return appearance_; }
    // The theme the previews paint with (the owner passes the active theme).
    void setTheme(const synth::theme::Theme& theme);

    // Override mode. The previews' names and tooltips mention `where` ("this module", "every Oscillator");
    // `followTooltip` explains what Follow falls back to.
    std::function<void(std::optional<synth::theme::KnobStyle> knob, std::optional<synth::theme::KnobStyle> fader)>
        onOverrideChanged;
    void setOverrideScope(const juce::String& where, const juce::String& followTooltip);
    // Does not fire onOverrideChanged.
    void setOverride(std::optional<synth::theme::KnobStyle> knob, std::optional<synth::theme::KnobStyle> fader);

    static constexpr int kPreferredHeight = 168;
    static constexpr int kOverrideHeight = 136;
    static constexpr int kOverrideWidth = 520;

    void resized() override;

    juce::Button& getStyleButtonForTest(int index);      // knob row
    juce::Button& getFaderStyleButtonForTest(int index); // fader row
    juce::Button& getFollowButtonForTest(bool fader);    // Override mode only
    juce::ToggleButton& getFamilyToggleForTest() { return familyToggle_; }

private:
    class StyleButton;
    enum class Kind { Knob, Fader };
    void styleChosen(Kind kind, int index); // index -1 is Follow
    StyleButton* buttonAt(Kind kind, int index) const;
    void setToggles(Kind kind, int index);
    void refreshButtonTexts();
    void refreshPreviewColour();

    synth::theme::KnobAppearance appearance_;
    synth::theme::Theme theme_;
    using Row = std::array<std::unique_ptr<StyleButton>, synth::theme::kKnobStyleCount>;
    Row buttons_;                                           // knob previews
    Row faderButtons_;                                      // fader previews
    std::unique_ptr<StyleButton> knobFollow_, faderFollow_; // Override mode only
    Mode mode_;
    std::optional<synth::theme::KnobStyle> knobOverride_, faderOverride_;
    juce::String where_{"this module"}, followTooltip_;
    juce::Label knobRowLabel_{{}, "Knobs"};
    juce::Label faderRowLabel_{{}, "Faders"};
    juce::ToggleButton familyToggle_{"Colour controls by module family"};
};

} // namespace synth::ui
