#pragma once

// The Wavetable card's tabbed control body: five pages of knobs/combos behind a row of tab buttons,
// with the performance controls pinned above the strip. ModuleComponent owns the controls; this
// component owns the tab buttons, the active page and the control -> page assignment, and lays the
// page body out (implementation and rationale: WavetableTabStrip.cpp).

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

class WavetableTabStrip : public juce::Component {
public:
    static constexpr int kNumPages = 5;
    static constexpr int kPinned = -1; // control lives above the strip, visible on every page
    static constexpr int kChrome = -2; // control is laid out with the display band, not by the strip

    // radioGroupId must be unique per card so two cards' tab buttons never share a radio group.
    explicit WavetableTabStrip(int radioGroupId);

    // Page owning the control whose display name is `controlName`, or kPinned / kChrome.
    static int pageFor(const juce::String& controlName);

    // Register a control and its caption; the page comes from the caption text. Both stay owned by
    // the caller and must outlive this component. Call before applyVisibility().
    void addSlider(juce::Slider& slider, juce::Label& label);
    void addCombo(juce::ComboBox& combo, juce::Label& label);

    int getActivePage() const { return activePage; }
    bool isChromeCombo(const juce::ComboBox& combo) const;

    // Shows only the pinned controls and the active page's, and syncs the tab toggle states.
    void applyVisibility();

    // Fired after a tab click changed the active page and applyVisibility() has run.
    std::function<void()> onPageChanged;

    // Height of the tallest page's body, so the card never resizes on a tab switch.
    int getTallestPageHeight() const;

    // Lays out the pinned row, this strip and the active page starting at `y`; returns the y below
    // the tallest page. With apply == false it only measures. Message thread only.
    int layoutBody(int y, int contentX, int contentW, bool apply);

    void resized() override;

private:
    struct SliderEntry {
        juce::Slider* control;
        juce::Label* label;
        int page;
    };
    struct ComboEntry {
        juce::ComboBox* control;
        juce::Label* label;
        int page;
    };

    void selectPage(int page);
    void layoutPinnedRow(int y, int contentX, int contentW);
    void layoutActivePage(int y, int contentX, int contentW);

    juce::OwnedArray<juce::TextButton> tabs;
    std::vector<SliderEntry> sliders;
    std::vector<ComboEntry> combos;
    int activePage = 0;
};
