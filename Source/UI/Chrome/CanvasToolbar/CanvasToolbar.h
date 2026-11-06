#pragma once

#include "UI/Chrome/ToolbarButton/ToolbarButton.h"
#include <array>
#include <juce_gui_basics/juce_gui_basics.h>

// CanvasToolbar (docs/layout/chrome.md#canvas-toolbar): the small strip on the canvas's own top row holding
// Add, Auto Arrange, Minimap and Matrix. The owner lays it out as the top row of whatever rectangle the canvas
// has (the main area, or the bottom panel's Canvas tab body) and hides it with the canvas, so it always
// travels with the canvas. It owns its four ToolbarButtons; the owner wires their onClick, titles, tooltips
// and icons, as it does for the top bar's buttons.
//
// One Tab stop: while it holds focus, Left/Right/Home/End move a ring across the visible, enabled buttons
// (no wrap) and Space/Return press the ringed one. Plain keys only; modified chords bubble up unhandled.
namespace synth::ui {

class CanvasToolbar : public juce::Component {
public:
    enum Slot { Add = 0, AutoArrange, Minimap, Matrix, NumSlots };

    // The strip's height in px: the ToolbarButton's chip, a one-line caption and a 1 px inset above and below.
    static constexpr int kHeight = 44;

    CanvasToolbar();

    ToolbarButton& addButton() noexcept { return add_; }
    ToolbarButton& autoArrangeButton() noexcept { return autoArrange_; }
    ToolbarButton& minimapButton() noexcept { return minimap_; }
    ToolbarButton& matrixButton() noexcept { return matrix_; }
    ToolbarButton& buttonAt(Slot slot) noexcept;
    const ToolbarButton& buttonAt(Slot slot) const noexcept;

    // The buttons carry captions (false) or only icons (true). The owner sets the buttons' text; this only
    // re-lays the strip, since a captioned button is wider than an icon-only one. Message thread only.
    void setIconOnly(bool iconOnly);
    bool isIconOnly() const noexcept { return iconOnly_; }

    // True while the strip sits in the dock's tab strip (the canvas is a panel tab): the buttons shrink to
    // InlineStrip chips with no caption, the strip draws no ground or rule of its own, and it is only as wide as
    // its buttons. Returns whether the mode changed; the owner then re-applies the captions (none in the strip).
    bool setInStrip(bool inStrip);
    bool isInStrip() const noexcept { return inStrip_; }
    // The width the strip needs in the dock's tab strip: its three chips and a right edge inset.
    static int stripWidth() noexcept;

    void resized() override;
    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;

    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType cause) override;

    // The slot the ring sits on: the last one used, or the first button until one is. -1 when no button is
    // both visible and enabled.
    int getFocusedSlot() const;
    // The spoken name of that button; empty when there is none.
    juce::String getFocusedButtonName() const;
    // Puts the ring on `slot` (when it is navigable) and takes keyboard focus. Message thread only.
    void focusSlot(Slot slot);

protected:
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    bool isNavigable(int slot) const;
    // The next navigable slot strictly past `from` in direction `step` (+1/-1); -1 when none.
    int navigableSlotFrom(int from, int step) const;
    void setFocusedSlot(int slot);

    ToolbarButton add_{"canvasAdd"};
    ToolbarButton autoArrange_{"autoArrange"};
    ToolbarButton minimap_{"toggleMinimap"};
    ToolbarButton matrix_{"toggleMatrix"};
    bool iconOnly_ = false;
    bool inStrip_ = false;
    int focusedSlot_ = -1;

    JUCE_LEAK_DETECTOR(CanvasToolbar)
};

} // namespace synth::ui
