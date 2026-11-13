#pragma once

#include "UI/Layout/UIAnimation.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace synth::ui {

enum class SidebarView : int { Modules, Sounds };

/** The two-way Modules / Sounds switch at the top of the left sidebar: one Tab stop, Left and Right switch views. */
class LibrarySidebarSwitch
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    static constexpr int kHeight = 36;

    /** The user chose a view (click or arrow key). */
    std::function<void(SidebarView)> onViewChosen;

    LibrarySidebarSwitch();
    ~LibrarySidebarSwitch() override;

    /** Moves the highlight without calling onViewChosen. */
    void setView(SidebarView view, bool animate);
    SidebarView getView() const { return view_; }
    /** The tooltips name each view's shortcut: "Modules (Cmd+B)". */
    void setSegmentTooltips(const juce::String& modules, const juce::String& sounds);
    juce::Rectangle<int> getSegmentBounds(SidebarView view) const;
    juce::String getAccessibilityValueText() const;
    void completeMotionForTest() { anim_.completeForTest(); }

    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType cause) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    void choose(SidebarView view);
    std::optional<SidebarView> segmentAt(juce::Point<int> p) const;

    SidebarView view_ = SidebarView::Modules;
    std::optional<SidebarView> hovered_;
    float pill_ = 0.0f; // 0 = under Modules, 1 = under Sounds
    juce::String modulesTip_ = "Modules", soundsTip_ = "Sounds: search and preview audio";
    std::optional<juce::VBlankAnimatorUpdater> vblank_;
    synth::ui::AnimationDriver anim_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibrarySidebarSwitch)
};

} // namespace synth::ui
