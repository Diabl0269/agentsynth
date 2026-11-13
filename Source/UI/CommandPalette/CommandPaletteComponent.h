#pragma once

#include "UI/Layout/FadeVisibility.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

class ModMatrixPicker;

/** The command palette's floating bar: a frame with a title strip and an (i) hint around a ModMatrixPicker. Drag
 *  its frame to move it inside its parent; the picker keeps keyboard focus. */
class CommandPaletteComponent : public juce::Component {
public:
    static constexpr int kWidth = 560;
    static constexpr int kHeight = 400;

    explicit CommandPaletteComponent(std::unique_ptr<ModMatrixPicker> picker);
    ~CommandPaletteComponent() override;

    ModMatrixPicker& getPicker() noexcept { return *picker_; }

    /** After a drag of the frame lands: the new top-left, in the parent's coordinates. */
    std::function<void(juce::Point<int>)> onMoved;

    /** Centred across `parent`, `topInset` below its top edge. */
    static juce::Point<int> defaultPosition(juce::Rectangle<int> parent, int topInset);
    /** `topLeft` moved just enough that the whole bar sits inside `parent`. */
    static juce::Point<int> clampedPosition(juce::Point<int> topLeft, juce::Rectangle<int> parent);

    /** Fades in; `fadeOut` fades out and calls `onHidden` once it has (at once when nothing can animate). */
    void fadeIn();
    void fadeOut(std::function<void()> onHidden);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    juce::Component& getInfoHintForTest() noexcept { return *infoHint_; }

private:
    std::unique_ptr<ModMatrixPicker> picker_;
    std::unique_ptr<juce::Component> infoHint_;
    juce::ComponentDragger dragger_;
    juce::ComponentBoundsConstrainer constrainer_;
    FadeVisibility fade_{this};
    bool dragging_ = false;
};

} // namespace synth::ui
