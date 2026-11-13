#pragma once

// The core pack sound a stand-in module waits to become (see docs/modules/sound-pack-downloads.md), kept in the
// module's saved view state.

#include "Modules/ModuleBase.h"

namespace synth {

inline juce::String pendingSoundOf(const ModuleBase& module) { return module.getCardViewState().pendingSound; }

inline void setPendingSound(ModuleBase& module, const juce::String& id) {
    auto state = module.getCardViewState();
    state.pendingSound = id;
    module.setCardViewState(state);
}

} // namespace synth
