#pragma once

#include <array>
#include <juce_gui_basics/juce_gui_basics.h>

// ToolbarComponent  (docs/layout/chrome.md#toolbar)
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
    ToolbarComponent();

    // Logical slot order — matches the left group [Library..AutoArrange] + right group
    // [ToggleMinimap, ToggleModMatrix, ToggleAiPanel, ToggleBottomPanel, ToggleTheme]. NumSlots is
    // the array size. Feedback sits immediately after Settings (same group — icon-only,
    // visually paired with the settings gear rather than a new standalone cluster).
    enum Slot {
        Library = 0,
        New,
        Save,
        Load,
        Settings,
        Feedback,
        Undo,
        Redo,
        AutoArrange,
        // ToggleMinimap sits before ToggleModMatrix so the right-hand group reads
        // minimap -> mod matrix -> AI panel -> bottom panel -> theme.
        ToggleMinimap,
        ToggleModMatrix,
        ToggleAiPanel,
        // The ONE bottom-dock open/close toggle, right before the theme toggle -- replaces
        // the former separate ToggleTimeline/ToggleMidiRemote slots.
        ToggleBottomPanel,
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
    // Focus-region outline (Source/UI/Layout/FocusRegion.h). This component owns no children of its
    // own (the buttons are direct children of MainComponent, see the class comment above), so
    // paintOverChildren vs. appending to paint() makes no practical difference here -- used anyway
    // for consistency with the other five focus-region roots.
    void paintOverChildren(juce::Graphics& g) override;

    // Roving keyboard focus (docs/layout/chrome.md#toolbar): the toolbar is one Tab stop; while it holds
    // focus, Left/Right/Home/End move a ring across the visible, enabled buttons (no wrap) and
    // Space/Return press the ringed one. Plain keys only; modified chords bubble up unhandled.
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType cause) override;

    // The slot the ring sits on: the last one used, or the first button until one is. -1 when no
    // button is both visible and enabled.
    int getFocusedSlot() const;
    // The spoken name of that button; empty when there is none.
    juce::String getFocusedButtonName() const;

protected:
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    bool isNavigable(int slot) const;
    // The next navigable slot strictly past `from` in direction `step` (+1/-1); -1 when none.
    int navigableSlotFrom(int from, int step) const;
    void setFocusedSlot(int slot);

    int focusedSlot_{-1};
    std::array<juce::DrawableButton*, NumSlots> buttons_{};
    int narrowThreshold_{480};
    bool narrowMode_{false};

    JUCE_LEAK_DETECTOR(ToolbarComponent)
};
