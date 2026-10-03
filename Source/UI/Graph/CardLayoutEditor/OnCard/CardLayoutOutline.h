#pragma once

// One control's outline in the on-card layout editor: a dashed accent outline with a grip in its
// bottom-right corner, solid with a faint wash on hover and keyboard focus, shadowed while lifted. It
// is an accessible button (Return asks for the control's options); the drag itself is the editor's, fed
// through the callbacks. docs/layout/module-card-layout.md#editing-a-layout.

#include "UI/Layout/UIAnimation.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

class CardLayoutOutline final : public juce::Button {
public:
    /** The component reaches this far past the control's cell, room for the lifted shadow. */
    static constexpr int kPad = 6;
    /** The outline is drawn this far inside the cell, so abutting cells' outlines never touch. */
    static constexpr int kInset = 3;
    static constexpr int kGripSize = 10;

    CardLayoutOutline(juce::String paramId, juce::String caption);

    const juce::String& getParamId() const noexcept { return paramId_; }
    /** Places the outline around the control cell `cell` (card pixels). */
    void setCell(juce::Rectangle<int> cell);
    /** Renames the control: the accessible title and tooltip follow its caption. */
    void setCaption(const juce::String& caption);
    /** 0..1: how far the control is lifted off the card while it is dragged. */
    void setLift(float lift);

    std::function<void(const juce::MouseEvent&)> onPress;
    std::function<void(const juce::MouseEvent&)> onDrag;
    std::function<void(const juce::MouseEvent&)> onRelease;
    /** Called first with every key; true = consumed. */
    std::function<bool(const juce::KeyPress&)> onKey;

    bool hitTest(int x, int y) override;
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }

    /** The outline's drawn rectangle, in this component's own coordinates. */
    juce::Rectangle<float> getOutlineArea() const;
    juce::Rectangle<float> getGripArea() const;

private:
    void animateHover(bool over);

    juce::String paramId_;
    float hover_ = 0.0f;
    float lift_ = 0.0f;
    juce::VBlankAnimatorUpdater updater_{this};
    AnimationDriver hoverAnim_;
};

} // namespace synth::ui
