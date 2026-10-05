#pragma once

// The window the mod dot's panel lives in: it draws the panel's one outline (body and arrow together), its shadow
// and its fill, and keeps the panel beside the dot, sliding it up or down as the panel grows so it stays on screen
// while the arrow stays level with the dot. Not modal: a click outside closes it (unless `keepOpenOnOutsideClick`
// says a canvas pick is under way), Esc closes it, and the panel's own controls take the keyboard.
// docs/modules/modulation.md#the-mod-dot-menu.

#include "ModDotPanelGeometry.h"
#include "UI/Layout/AppTooltipWindow.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

class ModDotPanelFrame final
    : public juce::Component
    , private juce::ComponentListener
    , private juce::Timer {
public:
    static constexpr int kShadow = 10; // transparent margin around the panel: the arrow and the shadow live in it

    /** `content` is the panel; `anchor` the dot it points at, `dot` its bounds and `area` the room the panel may use
     *  (a display's usable area), both in the space the frame's bounds are in (the screen, for a window). */
    ModDotPanelFrame(std::unique_ptr<juce::Component> content, juce::Component& anchor, juce::Rectangle<int> dot,
                     juce::Rectangle<int> area);
    ~ModDotPanelFrame() override;

    /** Gives the window its own tooltip: the main window's covers only its own component tree, so without this
     *  none of the panel's controls would ever show one. `appProperties` carries the "Show info tooltips"
     *  preference (may be null). Call before showOnDesktop. */
    void installTooltipWindow(juce::ApplicationProperties* appProperties);
    synth::ui::AppTooltipWindow* tooltipWindow() noexcept { return tooltipWindow_.get(); }

    /** Puts the frame on the desktop, in front, with the soft entrance every popup has. */
    void showOnDesktop();
    /** Closes through the soft exit; `onClosed` runs when it is gone (at once when it has no native window). */
    void close();
    bool isClosing() const noexcept { return closing_; }
    /** The room left for the panel's own height. */
    int maxContentHeight() const;
    /** Where the panel body sits, in the frame's own coordinates. */
    const modDotPanel::Placement& placement() const noexcept { return local_; }

    std::function<void()> onClosed;
    /** Asked on a press outside the panel; true keeps the panel open. */
    std::function<bool()> keepOpenOnOutsideClick;

    void paint(juce::Graphics& g) override;
    bool hitTest(int x, int y) override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    void reposition();
    void componentMovedOrResized(juce::Component& component, bool wasMoved, bool wasResized) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void timerCallback() override;

    std::unique_ptr<juce::Component> content_;
    juce::Component::SafePointer<juce::Component> anchor_;
    juce::Rectangle<int> area_;
    juce::Rectangle<int> dot_; // the anchor's bounds when the panel opened
    modDotPanel::Placement local_;
    juce::Path outline_;
    std::unique_ptr<AppTooltipWindow> tooltipWindow_;
    bool closing_ = false;
    bool listening_ = false;
};

} // namespace synth::ui
