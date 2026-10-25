// SequencerSongKeyTests.cpp -- the Sequencer and Poly Sequencer following the song key: a new one follows, a follower
// plays in the song key (and every note with No key or a foreign playhead), an own key ignores the song key, a stored
// node keeps what it played (a saved scale is its own key), and the transport hands the key to the audio thread.
#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/PolySequencerModule.h"
#include "Modules/SequencerModule.h"
#include "Music/SongKey.h"
#include "SequencerTestSupport.h"

using namespace seqtest;
using synth::AIStateMapper;
using synth::makeScale;
using synth::scalePresetIndex;
using synth::SequencerModuleBase;
using synth::TransportService;

namespace {

constexpr double kRate = 48000.0;

synth::SequencerPattern oneStep(const SequencerModuleBase& seq, int pitch) {
    auto pattern = seq.getPattern();
    pattern.steps[0].pitch = (std::uint8_t)pitch;
    pattern.steps[0].on = true;
    return pattern;
}

int firstNote(SequencerModule& seq) {
    const auto ons = onsOnly(render(seq, 512));
    return ons.empty() ? -1 : ons[0].note;
}

class SequencerSongKeyTest : public ::testing::Test {
protected:
    void SetUp() override {
        seq.prepareToPlay(kRate, 512);
        setNumber(seq, "steps", 1);
        run(seq, true);
    }

    void playFrom(int pitch) {
        seq.prepareToPlay(kRate, 512);
        seq.setPattern(oneStep(seq, pitch));
    }

    SequencerModule seq;
    TransportService transport;
};

SequencerModule* firstSequencer(juce::AudioProcessorGraph& graph) {
    for (auto* node : graph.getNodes())
        if (auto* module = dynamic_cast<SequencerModule*>(node->getProcessor()))
            return module;
    return nullptr;
}

SequencerModule* loadOne(juce::AudioProcessorGraph& graph, const juce::String& paramsJson) {
    const auto patch =
        juce::JSON::parse(R"({"nodes": [{"id": 1, "type": "Sequencer", "params": )" + paramsJson + "}]}");
    if (!AIStateMapper::applyJSONToGraph(patch, graph, true, /*trusted=*/true))
        return nullptr;
    return firstSequencer(graph);
}

} // namespace

TEST_F(SequencerSongKeyTest, ANewSequencerFollowsTheSongKey) {
    EXPECT_TRUE(seq.followsSongKey());
    PolySequencerModule poly;
    EXPECT_TRUE(poly.followsSongKey());
    EXPECT_FALSE(seq.getOwnKey().has_value());
}

TEST_F(SequencerSongKeyTest, AFollowerPlaysInTheSongKey) {
    transport.setSongKey(makeScale(2, scalePresetIndex("Major"))); // D major: F is out, E and F# are in
    seq.setPlayHead(&transport);
    playFrom(65);
    EXPECT_EQ(firstNote(seq), 64); // tie goes down
}

TEST_F(SequencerSongKeyTest, AFollowerMovesWhenTheSongKeyChanges) {
    seq.setPlayHead(&transport);
    transport.setSongKey(makeScale(2, scalePresetIndex("Major")));
    playFrom(65);
    EXPECT_EQ(firstNote(seq), 64);
    transport.setSongKey(makeScale(5, scalePresetIndex("Major"))); // F major has F
    playFrom(65);
    EXPECT_EQ(firstNote(seq), 65);
}

TEST_F(SequencerSongKeyTest, AFollowerWithNoKeyOrNoTransportPlaysEveryNote) {
    playFrom(66);
    EXPECT_EQ(firstNote(seq), 66) << "no playhead at all";
    seq.setPlayHead(&transport);
    playFrom(66);
    EXPECT_EQ(firstNote(seq), 66) << "the song has No key";
}

TEST_F(SequencerSongKeyTest, AnOwnKeyIgnoresTheSongKey) {
    transport.setSongKey(makeScale(2, scalePresetIndex("Major")));
    seq.setPlayHead(&transport);
    setNumber(seq, "followSongKey", 0.0f);
    setChoice(seq, "scaleRoot", "F");
    setChoice(seq, "scale", "Major");
    playFrom(65);
    EXPECT_EQ(firstNote(seq), 65);
    ASSERT_TRUE(seq.getOwnKey().has_value());
    EXPECT_EQ(synth::keyDisplayName(*seq.getOwnKey()), "F Major");
}

TEST_F(SequencerSongKeyTest, TheResolvedScaleIsWhatTheCardDraws) {
    const auto dDorian = makeScale(2, scalePresetIndex("Dorian"));
    EXPECT_TRUE(synth::sameKey(seq.resolvedScale(dDorian), dDorian));
    EXPECT_TRUE(seq.resolvedScale(std::nullopt).isChromatic());
    setNumber(seq, "followSongKey", 0.0f);
    setChoice(seq, "scaleRoot", "A");
    setChoice(seq, "scale", "Phrygian");
    const auto own = seq.resolvedScale(dDorian);
    EXPECT_EQ(own.rootPitchClass, 9);
    EXPECT_EQ(own.mask, makeScale(9, scalePresetIndex("Phrygian")).mask);
}

TEST(SequencerSongKeyValues, APresetIsStoredByIndexAndAnyOtherMaskAsCustom) {
    const auto preset = SequencerModuleBase::ownKeyValuesFor(makeScale(9, scalePresetIndex("Phrygian")));
    EXPECT_EQ(preset.root, 9);
    EXPECT_EQ(synth::sequencerScaleChoices()[preset.scaleIndex], "Phrygian");
    EXPECT_FALSE(preset.customMask.has_value());

    synth::MusicalScale hijaz;
    hijaz.rootPitchClass = 2;
    hijaz.mask = 0x05B3 ^ 0x0002; // not a preset
    const auto custom = SequencerModuleBase::ownKeyValuesFor(hijaz);
    EXPECT_EQ(custom.scaleIndex, synth::sequencerCustomScaleIndex());
    ASSERT_TRUE(custom.customMask.has_value());
    EXPECT_EQ(*custom.customMask, hijaz.mask);

    const auto none = SequencerModuleBase::ownKeyValuesFor(std::nullopt);
    EXPECT_EQ(none.scaleIndex, 0);
}

// Regression test for FRO1135: an old project's sequencer with a scale must keep playing it, not follow.
TEST(SequencerSongKeyLoad, AStoredScaleWithoutTheSwitchLoadsAsOwnKey) {
    juce::AudioProcessorGraph graph;
    auto* seq = loadOne(graph, R"({"scaleRoot": 2, "scale": 5})");
    ASSERT_NE(seq, nullptr);
    EXPECT_FALSE(seq->followsSongKey());
    ASSERT_TRUE(seq->getOwnKey().has_value());
    EXPECT_EQ(seq->getOwnKey()->rootPitchClass, 2);
}

TEST(SequencerSongKeyLoad, AStoredNodeWithNoScaleFollows) {
    juce::AudioProcessorGraph graph;
    auto* seq = loadOne(graph, R"({"run": true})");
    ASSERT_NE(seq, nullptr);
    EXPECT_TRUE(seq->followsSongKey());
}

TEST(SequencerSongKeyLoad, AStoredSwitchWinsOverTheRule) {
    juce::AudioProcessorGraph graph;
    auto* seq = loadOne(graph, R"({"scaleRoot": 2, "scale": 5, "followSongKey": true})");
    ASSERT_NE(seq, nullptr);
    EXPECT_TRUE(seq->followsSongKey());
    juce::AudioProcessorGraph second;
    auto* own = loadOne(second, R"({"followSongKey": false})");
    ASSERT_NE(own, nullptr);
    EXPECT_FALSE(own->followsSongKey());
}

TEST(SequencerSongKeyLoad, ASavedSequencerRoundTripsItsSwitch) {
    juce::AudioProcessorGraph graph;
    auto* seq = loadOne(graph, R"({"scaleRoot": 9, "scale": 6, "followSongKey": false})");
    ASSERT_NE(seq, nullptr);
    const auto saved = AIStateMapper::graphToJSON(graph);
    juce::AudioProcessorGraph reopened;
    ASSERT_TRUE(AIStateMapper::applyJSONToGraph(saved, reopened, true, /*trusted=*/true));
    auto* back = firstSequencer(reopened);
    ASSERT_NE(back, nullptr);
    EXPECT_FALSE(back->followsSongKey());
    ASSERT_TRUE(back->getOwnKey().has_value());
    EXPECT_EQ(back->getOwnKey()->rootPitchClass, 9);
}

TEST(TransportSongKeyMirror, TheAudioThreadSeesEveryKeyChange) {
    TransportService transport;
    EXPECT_EQ(transport.getSongKeyPacked(), 0u);
    const auto dDorian = makeScale(2, scalePresetIndex("Dorian"));
    transport.setSongKey(dDorian);
    const auto back = TransportService::unpackSongKey(transport.getSongKeyPacked());
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->rootPitchClass, 2);
    EXPECT_EQ(back->mask, dDorian.mask);
    transport.setSongKey(std::nullopt);
    EXPECT_EQ(transport.getSongKeyPacked(), 0u);
    EXPECT_FALSE(TransportService::unpackSongKey(0u).has_value());
}
