#pragma once

#include "Sequencer/SequencerModuleBase.h"

/** The chord step sequencer: the same pattern as the Sequencer with a chord per step instead of slide and filter
 *  amount (see Sequencer/SequencerPattern.h). */
class PolySequencerModule : public synth::SequencerModuleBase {
public:
    PolySequencerModule()
        : SequencerModuleBase("Poly Sequencer", true) {}

    ModuleType getModuleType() const override { return ModuleType::PolySequencer; }
};
