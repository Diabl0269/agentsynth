#pragma once

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

// MixerColourDot.h (docs/mixer/panel.md#the-colour-dot): the coloured dot at the left of a mixer column's header.
// A real juce::Button, so it is a Tab stop that takes Return and Space, carries a screen-reader name and a tooltip,
// and draws the shared focus ring; clicking it asks the owner to open the colour picker. A press that turns into a
// drag belongs to the header (the column is reordered by its header), so the dot forwards those events instead of
// treating them as a click.
namespace synth::ui {

class MixerColourDot : public juce::Button {
public:
    MixerColourDot();

    /** Message thread only. */
    void setColour(juce::Colour colour);
    juce::Colour getColour() const noexcept { return colour_; }

    /** The header's drag handle, fed the press, the drag and the release in the header's own coordinates. */
    struct DragForwarding {
        std::function<void(const juce::MouseEvent&)> onPress;
        std::function<void(const juce::MouseEvent&)> onDrag;
        /** Called after the button has seen the release; true when the press was a real drag, which swallows the click.
         */
        std::function<bool(const juce::MouseEvent&)> onRelease;
        std::function<bool()> isDragging;
    };
    DragForwarding dragForwarding;

    /** Call from the click handler: true once after a press that turned out to be a drag, so that click is ignored. */
    bool consumeDragFlag() noexcept;

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    juce::Colour colour_;
    bool suppressClick_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerColourDot)
};

} // namespace synth::ui
