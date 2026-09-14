#pragma once

// Shared test infrastructure for UndoRedo*Tests.cpp: the UndoRedoTest fixture (a bare graph +
// AppUndoManager pair) and findParam, the one helper reused outside the identity-preserving
// topic file.
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "Modules/ADSRModule.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/FilterModule.h"
#include "Modules/ModuleBase.h"
#include "Modules/OscillatorModule.h"
#include "Modules/VCAModule.h"
#include "PresetManager.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/LayoutUtil.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <set>
#include <vector>

class UndoRedoTest : public ::testing::Test {
protected:
    juce::AudioProcessorGraph graph;
    AppUndoManager undoManager;

    void SetUp() override { graph.clear(); }
};

namespace {

juce::RangedAudioParameter* findParam(juce::AudioProcessor* processor, const juce::String& paramId) {
    for (auto* param : processor->getParameters())
        if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param))
            if (p->paramID == paramId)
                return dynamic_cast<juce::RangedAudioParameter*>(param);
    return nullptr;
}
} // namespace
