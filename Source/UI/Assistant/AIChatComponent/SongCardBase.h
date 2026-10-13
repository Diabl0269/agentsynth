#pragma once

#include "UI/Layout/UIAnimation.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth {

// What the two song cards share: the raised panel they paint, the 160 ms fade-in a card takes when it first appears
// (nothing in the chat pops in), and the height contract with the chat. Height comes from layoutFor(width, apply):
// getRequiredHeight() measures with apply false and resized() places with apply true, so there is one geometry.
class SongCardBase : public juce::Component {
public:
    static constexpr int kPadding = 10;
    static constexpr int kGap = 8;

    /** Called when the card's height changed (a list opened, a notice appeared); the chat lays out again. */
    std::function<void()> onLayoutChanged;

    int getRequiredHeight(int width) { return layoutFor(width, false); }
    void resized() override { layoutFor(getWidth(), true); }
    void paint(juce::Graphics& g) override;
    void parentHierarchyChanged() override;

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override {
        return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::group);
    }

protected:
    explicit SongCardBase(bool fadeIn);
    ~SongCardBase() override;

    virtual int layoutFor(int width, bool apply) = 0;
    const synth::theme::Colors& colours() const { return synth::theme::themeOf(*this).colors; }
    void layoutChanged() {
        if (onLayoutChanged)
            onLayoutChanged();
        else
            resized();
    }

    juce::VBlankAnimatorUpdater vblank{this};

private:
    bool fadePending;
    synth::ui::AnimationDriver fadeAnim;
};

} // namespace synth
