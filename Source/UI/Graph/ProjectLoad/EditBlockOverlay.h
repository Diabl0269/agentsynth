#pragma once

// EditBlockOverlay.h -- while a slow project load runs, edits wait: a transparent layer over a surface (the canvas,
// the bottom dock) takes every click and says why, and hands scroll and zoom gestures to whatever is under it, so
// the user can look around but not change the patch. Never focusable and not in the accessibility tree.

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

class EditBlockOverlay final
    : public juce::Component
    , private juce::ComponentListener {
public:
    /** `covered` must outlive this overlay; the overlay is added by its owner to an ancestor of `covered`. */
    EditBlockOverlay(juce::Component& covered, std::function<void()> onRefused);
    ~EditBlockOverlay() override;

    /** Shows the overlay over `covered`'s current bounds, or hides it. */
    void setBlocking(bool blocking);

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify(const juce::MouseEvent& e, float scaleFactor) override;

private:
    void componentMovedOrResized(juce::Component&, bool, bool) override { follow(); }
    void follow();
    juce::Component& targetUnder(const juce::MouseEvent& e);
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    juce::Component& covered_;
    std::function<void()> onRefused_;
};

} // namespace synth::ui
