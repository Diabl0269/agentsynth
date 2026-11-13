#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// DockBackSlot.h (docs/mixer/panel.md#placement-and-detachable-windows): a detachable panel's toolbar has a slot at its
// right end for the "Dock back" button, filled only while the panel sits in its own window. The window itself has no
// header strip.
namespace synth::ui {

/** Implemented by every panel a DetachablePanelHost can detach. The default setDockBackButton() makes the button a
 * child of the panel itself and re-lays it out, for a panel that places it in its own layout (the Timeline); a panel
 * whose toolbar is a separate component overrides it to forward. */
class DockBackSlot {
public:
    virtual ~DockBackSlot() = default;
    /** Gives the panel the button to show at the right end of its toolbar (it becomes the panel's child), or nullptr
     *  to take it away again. The caller owns the button. */
    virtual void setDockBackButton(juce::Button* button) {
        auto& self = dynamic_cast<juce::Component&>(*this);
        adopt(self, dockBack_, button);
        self.resized(); // a panel with the button on its own layout (the Timeline's transport row) just re-lays out
    }
    /** The width a toolbar reserves for the button at its right edge. */
    static constexpr int kButtonWidth = 24;

    /** The shared body of setDockBackButton(): `slot` is the panel's pointer, `parent` the component that holds it. */
    static void adopt(juce::Component& parent, juce::Button*& slot, juce::Button* next) {
        if (slot == next)
            return;
        if (slot != nullptr)
            parent.removeChildComponent(slot);
        slot = next;
        if (slot != nullptr)
            parent.addAndMakeVisible(*slot);
    }

protected:
    juce::Button* dockBack_ = nullptr; // borrowed; a child of the panel while set (the default setDockBackButton)
};

} // namespace synth::ui
