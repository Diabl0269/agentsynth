// LfoMorphCursorModulationTests.cpp -- the LFO's Morph map cursor shows where the sound plays: the knob plus the
// Morph X / Y CV, as the Filter's does.
// Regression test for FRO1741: the LFO's Morph map cursor stayed on the knob while a cable moved the morph.
#include "Modules/LFOModule.h"
#include "ShapeEditor/ShapeEditorTestHelpers.h"
#include "UI/ModuleViews/MorphMap/MorphMap.h"
#include "UI/ModuleViews/ShapeEditor/LfoShapesAdapter.h"
#include "UI/ModuleViews/ShapeEditor/ShapeMapController.h"
#include <gtest/gtest.h>

using namespace synth::ui;
using namespace synth::ui::test;

namespace {

constexpr int kBlock = 256;
constexpr float kCV = 0.3f;

struct LfoRig {
    AudioEngine engine;
    LFOModule* lfo = nullptr;
    juce::AudioProcessorGraph::Node::Ptr node;
    AppUndoManager undo;
    std::shared_ptr<LfoShapesAdapter> adapter;

    LfoRig() {
        lfo = new LFOModule();
        node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(lfo));
        adapter = std::make_shared<LfoShapesAdapter>(*lfo, &undo, engine.getGraph(), node->nodeID, "LFO");
        lfo->prepareToPlay(48000.0, kBlock);
    }

    /** One block with `cv` held on the Morph X and Morph Y jacks. */
    void runBlock(float cv) {
        juce::AudioBuffer<float> buffer(lfo->getTotalNumInputChannels(), kBlock);
        buffer.clear();
        for (int i = 0; i < kBlock; ++i) {
            buffer.setSample(LFOModule::kMorphXChannel, i, cv);
            buffer.setSample(LFOModule::kMorphYChannel, i, cv);
        }
        juce::MidiBuffer midi;
        lfo->processBlock(buffer, midi);
    }
};

} // namespace

TEST(LfoMorphCursorModulation, TheModulatedMorphIsTheKnobPlusTheCv) {
    LfoRig rig;
    EXPECT_FLOAT_EQ(rig.lfo->getModulatedMorphX(), rig.lfo->getCurrentMorphX()) << "no block yet, no CV";
    rig.runBlock(kCV);
    EXPECT_NE(rig.lfo->getModulatedMorphX(), rig.lfo->getCurrentMorphX());
    EXPECT_NEAR(rig.lfo->getModulatedMorphX(), rig.lfo->getCurrentMorphX() + kCV, 1e-4f);
    EXPECT_NEAR(rig.lfo->getModulatedMorphY(), rig.lfo->getCurrentMorphY() + kCV, 1e-4f);
    rig.runBlock(0.0f);
    EXPECT_FLOAT_EQ(rig.lfo->getModulatedMorphX(), rig.lfo->getCurrentMorphX()) << "the cable let go";
}

TEST(LfoMorphCursorModulation, TheAdapterCursorIsTheModulatedMorph) {
    LfoRig rig;
    rig.runBlock(kCV);
    const auto cursor = rig.adapter->getCursor();
    EXPECT_NEAR(cursor.x, rig.lfo->getCurrentMorphX() + kCV, 1e-4f);
    EXPECT_NEAR(cursor.y, rig.lfo->getCurrentMorphY() + kCV, 1e-4f);
}

TEST(LfoMorphCursorModulation, TheMapCursorFollowsMorphCvAndNeverWritesTheKnob) {
    LfoRig rig;
    const float knobX = rig.lfo->getCurrentMorphX();
    MorphMap map;
    map.setSize(252, 150);
    ShapeMapController controller{map, *rig.adapter};
    controller.refresh();
    rig.runBlock(kCV);
    controller.syncCursor();
    EXPECT_NEAR(map.getCursor().x, knobX + kCV, 1e-4f);
    EXPECT_FLOAT_EQ(rig.lfo->getCurrentMorphX(), knobX) << "showing the modulation never writes the knob";
}
