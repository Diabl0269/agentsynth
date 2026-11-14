// FilterPitchAutoCableTests.cpp: the Custom filter's Pitch cable that a connection event adds by itself
// (PortConnector::connectJacks): only when the cable just dropped gave a Filter in By hand or By pitch its first Pitch
// source, as the same undo step; never on a project load, never again for a cable the user deleted.
// docs/layout/cables.md#custom-filter-pitch-cable.

#include "PortPanelTestFixture.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "AudioEngine/ConnectionIndex.h"
#include "Mixer/ChannelFlows/UpstreamPitchSource.h"
#include "Modules/FilterModule.h"
#include "Modules/PolyMidiModule.h"
#include "UI/Graph/PortPanel/PortConnector.h"

namespace {

using NodeID = juce::AudioProcessorGraph::NodeID;
using Mode = synth::CustomFilterPerNote::Mode;

// Poly MIDI, the oscillator and a Custom Filter, on a real editor, with nothing cabled yet.
struct PitchFixture : PortFixture {
    NodeID poly, filter;

    explicit PitchFixture(Mode mode, bool custom = true)
        : PortFixture(0, 0) {
        auto& graph = engine.getGraph();
        auto polyNode = graph.addNode(std::make_unique<PolyMidiModule>());
        polyNode->properties.set("x", 40);
        polyNode->properties.set("y", 400);
        poly = polyNode->nodeID;
        auto filterNode = graph.addNode(std::make_unique<FilterModule>());
        filterNode->properties.set("x", 500);
        filterNode->properties.set("y", 40);
        filter = filterNode->nodeID;
        engine.updateModuleNames();
        refresh();
        sizeModuleComponents(*editor);
        refresh();
        auto* f = static_cast<FilterModule*>(module(filter));
        if (custom) {
            for (auto* p : f->getParameters())
                if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(p);
                    choice && choice->paramID == "filterType")
                    *choice = FilterModule::kCustomTypeIndex;
        }
        synth::CustomFilterPerNote perNote;
        perNote.mode = mode;
        f->setCustomPerNote(perNote);
    }

    int oscPitchJack() { return module(oscId)->mapInputChannel(rawPitchChannel(*module(oscId))).visibleJackIndex; }
    static int rawPitchChannel(ModuleBase& m) {
        for (int ch = 0; ch < 40; ++ch)
            if (m.mapInputChannel(ch).role == PortRole::Pitch)
                return ch;
        return -1;
    }
    int filterPitchJack() { return module(filter)->mapInputChannel(FilterModule::kPitchBase).visibleJackIndex; }

    void cableOscToFilter() { synth::ui::PortConnector::connectJacks(*editor, oscId, 0, filter, 0, false, true); }
    void cablePolyToOsc() {
        synth::ui::PortConnector::connectJacks(*editor, poly, 0, oscId, oscPitchJack(), false, true);
    }
    bool pitchCabled() {
        const synth::ConnectionIndex cables(engine.getGraph());
        return synth::hasPitchCable(cables, engine.getGraph(), filter);
    }
};

} // namespace

TEST_F(GraphEditorTest, TheCableThatGivesAByPitchFilterItsFirstSourceAlsoCablesItsPitchInput) {
    PitchFixture f(Mode::ByPitch);
    f.cableOscToFilter();
    ASSERT_FALSE(f.pitchCabled()) << "no pitch source upstream yet, so no cable";

    f.cablePolyToOsc();

    EXPECT_TRUE(f.pitchCabled()) << "the new source reached the filter's Pitch input";
}

TEST_F(GraphEditorTest, TheAutoPitchCableIsTheSameUndoStepAsTheCableThatCausedIt) {
    PitchFixture f(Mode::ByPitch);
    f.cableOscToFilter();
    f.cablePolyToOsc();
    ASSERT_TRUE(f.pitchCabled());

    ASSERT_TRUE(f.undo.undo());

    EXPECT_FALSE(f.pitchCabled());
    EXPECT_EQ(countAudioConnectionsBetween(f.engine.getGraph(), f.poly, f.oscId), 0) << "the source cable went with it";
    EXPECT_EQ(countAudioConnectionsBetween(f.engine.getGraph(), f.oscId, f.filter), 1) << "the earlier cable stays";
}

TEST_F(GraphEditorTest, TheOtherBuildOrderCablesPitchWhenTheFilterIsFedLast) {
    PitchFixture f(Mode::ByPitch);
    f.cablePolyToOsc();
    ASSERT_FALSE(f.pitchCabled());

    f.cableOscToFilter();

    EXPECT_TRUE(f.pitchCabled());
}

TEST_F(GraphEditorTest, ByHandGetsTheSameAutoPitchCable) {
    PitchFixture f(Mode::ByHand);
    f.cableOscToFilter();
    f.cablePolyToOsc();
    EXPECT_TRUE(f.pitchCabled());
}

TEST_F(GraphEditorTest, NoAutoPitchCableWhenTheFilterIsOffOrNotCustom) {
    {
        PitchFixture f(Mode::Off);
        f.cableOscToFilter();
        f.cablePolyToOsc();
        EXPECT_FALSE(f.pitchCabled()) << "per-note Off does not use Pitch";
    }
    {
        PitchFixture f(Mode::ByPitch, /*custom=*/false);
        f.cableOscToFilter();
        f.cablePolyToOsc();
        EXPECT_FALSE(f.pitchCabled()) << "a non-Custom filter has no per-note modes";
    }
}

TEST_F(GraphEditorTest, ADeletedPitchCableIsNotBroughtBackByAnUnrelatedCable) {
    PitchFixture f(Mode::ByPitch);
    f.cableOscToFilter();
    f.cablePolyToOsc();
    ASSERT_TRUE(f.pitchCabled());
    auto& graph = f.engine.getGraph();
    for (const auto& c : graph.getConnections())
        if (c.destination.nodeID == f.filter && c.source.nodeID == f.poly)
            graph.removeConnection(c);
    ASSERT_FALSE(f.pitchCabled());

    synth::ui::PortConnector::connectJacks(*f.editor, f.poly, 0, f.oscId, f.oscPitchJack(), false, true);

    EXPECT_FALSE(f.pitchCabled()) << "a source that was already there is not a new source";
}

// Regression test for FRO1453: a project saved with the filter in By pitch and no Pitch cable must load as it was
// saved.
TEST_F(GraphEditorTest, LoadingAProjectWithAByPitchFilterAndNoPitchCableAddsNone) {
    {
        PitchFixture f(Mode::ByPitch);
        f.cableOscToFilter();
        f.cablePolyToOsc();
        auto& graph = f.engine.getGraph();
        for (const auto& c : graph.getConnections())
            if (c.destination.nodeID == f.filter && c.source.nodeID == f.poly)
                graph.removeConnection(c);
        ASSERT_FALSE(f.pitchCabled());
        const auto json = synth::AIStateMapper::graphToJSON(graph);

        AudioEngine reloaded;
        ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(json, reloaded.getGraph(), /*clearExisting=*/true,
                                                           /*trusted=*/true));
        FilterModule* loadedFilter = nullptr;
        for (auto* node : reloaded.getGraph().getNodes())
            if (auto* m = dynamic_cast<FilterModule*>(node->getProcessor()))
                loadedFilter = m;
        ASSERT_NE(loadedFilter, nullptr);
        ASSERT_EQ(loadedFilter->getCustomPerNote().mode, Mode::ByPitch) << "the saved mode came back";
        const synth::ConnectionIndex cables(reloaded.getGraph());
        for (auto* node : reloaded.getGraph().getNodes())
            if (dynamic_cast<FilterModule*>(node->getProcessor()))
                EXPECT_FALSE(synth::hasPitchCable(cables, reloaded.getGraph(), node->nodeID))
                    << "a load never adds the cable";
    }
}
