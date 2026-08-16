#pragma once

#include <array>
#include <juce_gui_basics/juce_gui_basics.h>

// ToolbarComponent  §2.2
// FlexBox-based responsive top strip. Owns NO buttons itself — the buttons are direct
// children of MainComponent (so existing getChildren() accessors keep working). This
// component only paints the toolbar background and positions the buttons passed via
// setButtons() using a single juce::FlexBox pass.
//
// Narrow mode: when the laid-out width drops below the threshold (default 480 px) every
// button collapses to a 32 px icon-only preferred width. The caller compares
// isNarrowMode() against its cached value and re-applies icon-only/icon+text text ONLY on
// the mode transition (so Drawable clone work is gated, not per-resize).
//
// Headless-safe: paint() dynamic_casts the LookAndFeel and falls back to a literal bg
// colour when the themed LnF is absent (test runner has no themed LnF installed).
class ToolbarComponent : public juce::Component {
public:
    // Logical slot order — matches the left group [Library..AutoArrange] + right group
    // [ToggleMinimap, ToggleModMatrix, ToggleAiPanel, ToggleTimeline, ToggleTheme]. NumSlots is
    // the array size.
    enum Slot {
        Library = 0,
        New,
        Save,
        Load,
        Settings,
        Undo,
        Redo,
        AutoArrange,
        // ToggleMinimap sits before ToggleModMatrix so the right-hand group reads
        // minimap -> mod matrix -> AI panel -> timeline -> theme (issue #159).
        ToggleMinimap,
        ToggleModMatrix,
        ToggleAiPanel,
        // Timeline panel toggle, right before the theme toggle. Present unconditionally
        // here even when SYNTH_ENABLE_TIMELINE is OFF — MainComponent simply leaves this slot's
        // button pointer null in that build, and layoutButtons() already skips null slots.
        ToggleTimeline,
        ToggleTheme,
        NumSlots
    };

    // Inject the (non-owning) button pointers. Buttons remain children of MainComponent.
    void setButtons(std::array<juce::DrawableButton*, NumSlots> btns);

    // Run the FlexBox layout against `bounds` and set each child button's bounds.
    // Records narrowMode_ (queryable via isNarrowMode()) for the caller's transition gate.
    void layoutButtons(juce::Rectangle<int> bounds);

    void setNarrowModeThreshold(int px) { narrowThreshold_ = px; }
    bool isNarrowMode() const noexcept { return narrowMode_; }

    void paint(juce::Graphics& g) override;

private:
    std::array<juce::DrawableButton*, NumSlots> buttons_{};
    int narrowThreshold_{480};
    bool narrowMode_{false};

    JUCE_LEAK_DETECTOR(ToolbarComponent)
};
