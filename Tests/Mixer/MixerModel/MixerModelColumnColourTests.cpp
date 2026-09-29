// MixerModelColumnColourTests.cpp (docs/mixer/panel.md#what-the-mixer-shows): a column's swatch colour is the
// linked track's colour; shared/orphan channels keep the macro colour. Headless, like MixerModelColumnTests.cpp.
#include "Mixer/MixerModel/MixerModel.h"
#include "MixerModelTestFixture.h"

TEST(MixerModelColumnColourTests, ALinkedChannelTakesItsTracksColourOverTheMacroColour) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    synth::TimelineDoc doc;
    synth::MacroSet macros;

    const auto trackA = doc.addTrack(synth::TrackKind::Audio, "Drums");
    const auto trackB = doc.addTrack(synth::TrackKind::Audio, "Bass");
    const auto rigA = buildLinearChannelRigMMT(graph, doc, trackA);
    const auto rigB = buildLinearChannelRigMMT(graph, doc, trackB);
    ASSERT_NE(rigA.strip, nullptr);
    ASSERT_NE(rigB.strip, nullptr);
    doc.setTrackColour(trackA, 0xffe0503c);
    doc.setTrackColour(trackB, 0xff3cb0e0);

    synth::Macro macro;
    macro.name = "Drum Bus";
    macro.colour = juce::Colour(0xffaa00aa);
    macro.members.push_back(rigA.strip->properties["uuid"].toString());
    macros.add(macro);

    auto snapshot = synth::buildMixerSnapshot(graph, doc, macros);
    ASSERT_EQ(snapshot.columns.size(), 2u);
    EXPECT_EQ(snapshot.columns[0].colour, juce::Colour(0xffe0503c)) << "track colour beats the macro colour";
    EXPECT_EQ(snapshot.columns[1].colour, juce::Colour(0xff3cb0e0)) << "each column follows its OWN track";

    doc.setTrackColour(trackB, 0xff11aa11);
    snapshot = synth::buildMixerSnapshot(graph, doc, macros);
    EXPECT_EQ(snapshot.columns[1].colour, juce::Colour(0xff11aa11)) << "a colour change shows on the next snapshot";
}

TEST(MixerModelColumnColourTests, AChannelSharedByTwoTracksKeepsTheMacroColour) {
    AudioEngine engine;
    auto& graph = engine.getGraph();
    graph.setPlayConfigDetails(0, 2, 44100.0, 512);
    synth::TimelineDoc doc;
    synth::MacroSet macros;

    const auto trackA = doc.addTrack(synth::TrackKind::Midi, "Kick");
    const auto trackB = doc.addTrack(synth::TrackKind::Midi, "Snare");
    doc.setTrackColour(trackA, 0xffe0503c);
    doc.setTrackColour(trackB, 0xff3cb0e0);
    juce::String inAUuid, inBUuid, unused, stripUuid;
    auto* inA = addPlainNodeMMT(graph, "Track In", inAUuid);
    auto* inB = addPlainNodeMMT(graph, "Track In", inBUuid);
    auto* instrument = addPlainNodeMMT(graph, "Sampler", unused);
    auto* strip = addPlainNodeMMT(graph, "Channel Strip", stripUuid);
    ASSERT_NE(inA, nullptr);
    ASSERT_NE(inB, nullptr);
    ASSERT_NE(instrument, nullptr);
    ASSERT_NE(strip, nullptr);
    if (auto* stripModule = dynamic_cast<ChannelStripModule*>(strip->getProcessor()))
        stripModule->setShape(ChannelStripModule::Shape::Stereo);
    constexpr int midi = juce::AudioProcessorGraph::midiChannelIndex;
    graph.addConnection({{inA->nodeID, midi}, {instrument->nodeID, midi}});
    graph.addConnection({{inB->nodeID, midi}, {instrument->nodeID, midi}});
    connectStereoMMT(graph, *instrument, *strip);
    doc.setTrackBinding(trackA, inAUuid);
    doc.setTrackBinding(trackB, inBUuid);

    synth::Macro macro;
    macro.name = "Drum Kit";
    macro.colour = juce::Colour(0xffaa00aa);
    macro.members.push_back(stripUuid);
    macros.add(macro);

    const auto snapshot = synth::buildMixerSnapshot(graph, doc, macros);
    ASSERT_EQ(snapshot.columns.size(), 1u);
    EXPECT_EQ(snapshot.columns[0].colour, juce::Colour(0xffaa00aa)) << "two feeders: no single track colour";
}
