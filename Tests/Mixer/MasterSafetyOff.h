#pragma once

// For tests that measure the mix itself (sums, hot levels, "the sound did not change"): Master's safety limiter
// would reshape a peak above -1 dBFS, which is not what those tests are about. docs/mixer/mixer.md#safety-limiter.

#include "Modules/MasterModule.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace testutil {

inline void turnOffMasterSafetyLimiter(MasterModule& master) {
    for (auto* param : master.getParameters())
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param);
            p != nullptr && p->paramID == MasterModule::kSafetyLimiterId)
            param->setValueNotifyingHost(0.0f);
}

inline void turnOffMasterSafetyLimiter(juce::AudioProcessorGraph& graph) {
    for (auto* node : graph.getNodes())
        if (auto* master = dynamic_cast<MasterModule*>(node->getProcessor()))
            turnOffMasterSafetyLimiter(*master);
}

} // namespace testutil
