#pragma once

// A split button: two side-by-side halves in one rounded pill, each a Tab stop with the accent focus ring, a
// screen-reader name and a tooltip, drawn lit while its mode is on. A half is an icon, or an icon and a label.
// Left/Right move between the halves; Up/Down are left to the owner. Shared by the mod dot panel's "Add source"
// and the hosted plugin card's "Add controls", so the two look identical.
// The icons and colours come from the mod dot panel's glyph set and palette (UI/Graph/ModDot).

#include "UI/Graph/ModDot/ModDotGlyphButton.h"
#include "UI/Graph/ModDot/ModDotMotion.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

/** What one half shows and says. An empty `label` makes it icon only (the icon centred). */
struct SplitButtonHalfSpec {
    ModDotGlyph glyph = ModDotGlyph::List;
    juce::String label;
    juce::String title;    // screen-reader name
    juce::String litTitle; // screen-reader name while lit; empty keeps `title`
    juce::String tooltip;
};

class SplitButton final : public juce::Component {
public:
    static constexpr int kHeight = 26;

    SplitButton(const SplitButtonHalfSpec& left, const SplitButtonHalfSpec& right, const juce::String& groupTitle);
    ~SplitButton() override;

    juce::Button& leftHalf() noexcept;
    juce::Button& rightHalf() noexcept;
    /** Lights the half (and gives a screen reader its lit name); the right half is a toggle for assistive tech. */
    void setLeftLit(bool lit);
    void setRightLit(bool lit);
    bool isLeftLit() const noexcept { return leftLit_; }
    bool isRightLit() const noexcept { return rightLit_; }

    void resized() override;
    void paintOverChildren(juce::Graphics& g) override;

private:
    class Half;
    std::unique_ptr<Half> left_;
    std::unique_ptr<Half> right_;
    bool leftLit_ = false;
    bool rightLit_ = false;
    bool iconsOnly_ = false;
};

} // namespace synth::ui
