#pragma once

#include "UI/Layout/TextLinkButton.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/**
 * The one line a Poly Sequencer card shows when its Midi output feeds a module that plays one note at a time:
 * "Next: Oscillator plays one note at a time" and a "Turn Poly on" link. It sits in the page strip's row and
 * holds no state of its own: the card names the module (setModule) and answers the link (onTurnPolyOn).
 * docs/modules/sequencer-design.md#poly-sequencer-chords.
 */
class StepGridPolyHint
    : public juce::Component
    , public juce::SettableTooltipClient {
public:
    StepGridPolyHint();
    ~StepGridPolyHint() override;

    /** The words of the line for `moduleName`, shared with the chord picker's footer. */
    static juce::String lineFor(const juce::String& moduleName);

    /** Names the module that plays one note at a time; empty hides the hint. */
    void setModule(const juce::String& moduleName);
    const juce::String& getModule() const noexcept { return module_; }
    TextLinkButton& getLink() noexcept { return link_; }
    /** The width the whole line wants; the grid shows it only when the row's free side is at least this wide. */
    int preferredWidth() const;

    /** The link was pressed (click, Space or Enter). */
    std::function<void()> onTurnPolyOn;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    juce::String module_;
    TextLinkButton link_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepGridPolyHint)
};

} // namespace synth::ui
