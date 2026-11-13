// MainViewHolder.cpp -- the main area's container: lays its child over its whole area.
#include "UI/Layout/MainViewHolder.h"

namespace synth::ui {

// An empty holder must never swallow a click meant for the canvas under it.
MainViewHolder::MainViewHolder() {
    setInterceptsMouseClicks(false, true);
    setComponentID("mainViewHolder");
}

void MainViewHolder::resized() {
    for (auto* child : getChildren())
        child->setBounds(getLocalBounds());
}

// A host moved in is laid out at once, before the next window layout pass.
void MainViewHolder::childrenChanged() { resized(); }

} // namespace synth::ui
