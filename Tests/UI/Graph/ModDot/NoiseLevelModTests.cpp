// The Noise card's Level knob takes a modulation source like any other knob: dropping an LFO on it
// creates a routing and the knob shows its ring and dot.

#include "../ModuleComponent/ModuleComponentTestFixture.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"

#include "../GraphEditor/GraphEditorTestHelpers.h"
#include "Modules/LFOModule.h"
#include "Modules/NoiseModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModDot/KnobModSources.h"
#include <gtest/gtest.h>

namespace {

struct NoiseCanvas {
    AudioEngine engine;
    AppUndoManager undo;
    std::unique_ptr<GraphEditor> editor;
    juce::AudioProcessorGraph::NodeID lfoId, noiseId;

    NoiseCanvas() {
        engine.initialise();
        engine.getGraph().clear();
        editor = std::make_unique<GraphEditor>(engine, &undo);
        undo.setGraphEditor(editor.get());
        lfoId = engine.getGraph().addNode(std::make_unique<LFOModule>())->nodeID;
        noiseId = engine.getGraph().addNode(std::make_unique<NoiseModule>())->nodeID;
        editor->updateComponents();
    }
    ~NoiseCanvas() {
        editor.reset();
        engine.shutdown();
    }
    int channelFor(const char* paramId) {
        auto* mb = dynamic_cast<ModuleBase*>(engine.getGraph().getNodeForId(noiseId)->getProcessor());
        return mb->modulationChannelForParam(paramId);
    }
};

} // namespace

TEST(NoiseLevelMod, DroppingASourceOnTheLevelKnobCreatesARoutingAndARing) {
    NoiseCanvas c;
    const int level = c.channelFor("level");
    ASSERT_GE(level, 0);
    const auto atten = c.editor->connectModulationSource(c.lfoId, 0, c.noiseId, level, 0.5f);
    EXPECT_NE(atten.uid, 0u);
    c.editor->timerCallback();
    const auto sources = synth::ui::knobModSources(*c.editor, c.noiseId, level);
    EXPECT_EQ(sources.size(), 1u);
}
