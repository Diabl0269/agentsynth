#pragma once

#include "Sequencer/SequencerModuleBase.h"

/** The monophonic step sequencer: up to 128 steps of pitch, gate, velocity, chance, repeat, slide, nudge,
 *  condition and filter amount, held as module state (see Sequencer/SequencerPattern.h). */
class SequencerModule : public synth::SequencerModuleBase {
public:
    SequencerModule()
        : SequencerModuleBase("Sequencer", false) {}

    ModuleType getModuleType() const override { return ModuleType::Sequencer; }
};
