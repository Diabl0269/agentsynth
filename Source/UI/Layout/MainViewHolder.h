#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/** The main area's container for a view other than the canvas (docs/layout/chrome.md#the-main-view): the
 *  bottom dock moves that view's panel host in here and takes it back when it leaves. Fills its one child,
 *  paints nothing and takes no clicks of its own. */
class MainViewHolder : public juce::Component {
public:
    MainViewHolder();

    void resized() override;
    void childrenChanged() override;
};

} // namespace synth::ui
