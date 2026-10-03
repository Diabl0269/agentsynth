#pragma once

// One page of the mod dot's panel (sources, add source). The panel swaps pages in place: it asks each for the
// height it wants, and a page tells it when that changes (a row growing in, a group folding).

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

class ModDotPage : public juce::Component {
public:
    static constexpr int kWidth = 280;

    /** The height the page wants at its width right now (it changes frame by frame while rows move). */
    virtual int preferredHeight() const = 0;
    /** Puts keyboard focus on the page's natural first control. */
    virtual void focusEntry() = 0;

    std::function<void()> onHeightChanged;

protected:
    void heightChanged() {
        if (onHeightChanged)
            onHeightChanged();
    }
};

} // namespace synth::ui
