// DrumKitPadsTests.cpp
// The Drum Kit's pad table: the default pads and the count rule, the saved state (pads beyond the count keep their
// settings), the group parameters writing their pads (the hat split included, automation too), two pads on one drum
// both sounding, the hat choke across pads, a file pad playing from its Start, and the Recorded switches and a kit
// pick moving pads between the kit and the built-in drums.
#include "../Sampler/SamplerTestWavs.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/DrumKit/DrumKitModule.h"
#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>

using namespace sampler_test;
using synth::drums::PadKind;
using PadParam = DrumKitModule::PadParam;

namespace {

constexpr double kRate = 44100.0;
constexpr int kBlock = 256;

struct Hit {
    int note;
    float velocity = 1.0f;
    int atSample = 0;
};

std::vector<float> render(DrumKitModule& module, const std::vector<Hit>& hits, double seconds) {
    const int total = (int)(seconds * kRate);
    std::vector<float> out;
    for (int start = 0; start < total; start += kBlock) {
        const int n = std::min(kBlock, total - start);
        juce::AudioBuffer<float> block(2, n);
        block.clear();
        juce::MidiBuffer midi;
        for (const auto& hit : hits)
            if (hit.atSample >= start && hit.atSample < start + n)
                midi.addEvent(juce::MidiMessage::noteOn(10, hit.note, hit.velocity), hit.atSample - start);
        module.processBlock(block, midi);
        out.insert(out.end(), block.getReadPointer(0), block.getReadPointer(0) + n);
    }
    return out;
}

std::unique_ptr<DrumKitModule> makeKit() {
    auto kit = std::make_unique<DrumKitModule>();
    kit->prepareToPlay(kRate, kBlock);
    return kit;
}

float rmsOf(const std::vector<float>& s, double fromSec, double toSec) {
    const auto a = (size_t)(fromSec * kRate);
    const auto b = std::min(s.size(), (size_t)(toSec * kRate));
    double sum = 0.0;
    for (size_t i = a; i < b; ++i)
        sum += (double)s[i] * s[i];
    return b > a ? (float)std::sqrt(sum / (double)(b - a)) : 0.0f;
}

// What is left of `with` once `without` is taken away: the sound of the hits only `with` had.
std::vector<float> minus(const std::vector<float>& with, const std::vector<float>& without) {
    std::vector<float> out(std::min(with.size(), without.size()));
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = with[i] - without[i];
    return out;
}

void setActual(DrumKitModule& module, const juce::String& id, float value) {
    auto* p = dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(&module, id));
    ASSERT_NE(p, nullptr) << id;
    p->setValueNotifyingHost(p->convertTo0to1(value));
}

float actual(DrumKitModule& module, int pad, PadParam which) { return module.padParameter(pad, which)->get(); }

} // namespace

TEST(DrumKitPads, ANewKitHasEightPadsOnTheGeneralMidiNotesAndThirtyTwoSlots) {
    DrumKitModule kit;
    EXPECT_EQ(kit.padCount(), 8);
    const std::vector<std::pair<int, const char*>> expected = {
        {36, "Kick"},          {38, "Snare"},          {39, "Clap"},        {42, "Closed hat"}, {46, "Open hat"},
        {45, "Tom"},           {49, "Cymbal"},         {56, "Cowbell"},     {37, "Rim"},        {44, "Pedal hat"},
        {41, "Low floor tom"}, {43, "High floor tom"}, {47, "Low-mid tom"}, {48, "Hi-mid tom"}, {50, "High tom"},
        {51, "Ride"},          {35, "Kick 2"},         {40, "Snare 2"},     {57, "Crash 2"},    {59, "Ride 2"}};
    for (size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(kit.pad((int)i).note, expected[i].first) << "pad " << i + 1;
        EXPECT_EQ(kit.pad((int)i).sound, expected[i].first) << "pad " << i + 1;
        EXPECT_EQ(kit.pad((int)i).name, expected[i].second) << "pad " << i + 1;
        EXPECT_EQ(kit.pad((int)i).kind, PadKind::Kit);
    }
    for (int i = 20; i < synth::drums::kMaxPads; ++i) {
        EXPECT_EQ(kit.pad(i).sound, synth::drums::kNoSound) << "a free pad is silent until shown";
        EXPECT_TRUE(kit.pad(i).shown);
    }
    EXPECT_NE(findParameterByID(&kit, "pad32Start"), nullptr);
    EXPECT_EQ(kit.padParameter(0, PadParam::Level)->getName(32), "Pad 1 level");
}

// Each pad knob reads in its unit; a raw "0.0000" under Tune or Decay told the user nothing.
TEST(DrumKitPads, PadValuesReadInTheirUnits) {
    DrumKitModule kit;
    auto text = [&](PadParam which, float value) {
        juce::AudioProcessorParameter* p = kit.padParameter(0, which);
        return p->getText(kit.padParameter(0, which)->convertTo0to1(value), 32);
    };
    EXPECT_EQ(text(PadParam::Level, 0.8f), "80 %");
    EXPECT_EQ(text(PadParam::Tune, -3.0f), "-3.0 st");
    EXPECT_EQ(text(PadParam::Decay, 0.3f), "300 ms");
    EXPECT_EQ(text(PadParam::Decay, 1.5f), "1.50 s");
    auto* decay = kit.padParameter(0, PadParam::Decay);
    EXPECT_NEAR(decay->convertFrom0to1(static_cast<juce::AudioProcessorParameter*>(decay)->getValueForText("1.5 s")),
                1.5f, 1e-3f);
}

TEST(DrumKitPads, TheCountIsFourToThirtyTwoInStepsOfFour) {
    EXPECT_EQ(synth::drums::clampPadCount(0), 4);
    EXPECT_EQ(synth::drums::clampPadCount(3), 4);
    EXPECT_EQ(synth::drums::clampPadCount(10), 12);
    EXPECT_EQ(synth::drums::clampPadCount(16), 16);
    EXPECT_EQ(synth::drums::clampPadCount(99), 32);
    DrumKitModule kit;
    kit.setPadCount(33);
    EXPECT_EQ(kit.padCount(), 32);
    kit.setPadCount(-4);
    EXPECT_EQ(kit.padCount(), 4);
}

TEST(DrumKitPads, ShowingAFreePadGivesItASoundAndItsGroupsValuesSoItPlaysAtOnce) {
    auto kit = makeKit();
    setActual(*kit, "cymbalDecay", 2.5f);
    EXPECT_LT(rmsOf(render(*kit, {{52}}, 0.3), 0.0, 0.3), 1.0e-6f) << "pad 21 is silent while hidden";
    kit->setPadCount(24);
    EXPECT_EQ(kit->pad(20).sound, 49) << "China plays the crash";
    EXPECT_NEAR(actual(*kit, 20, PadParam::Decay), 2.5f, 1.0e-4f);
    EXPECT_GT(rmsOf(render(*kit, {{52}}, 0.3), 0.0, 0.3), 1.0e-3f);
}

TEST(DrumKitPads, TheStateRoundTripsAndPadsBeyondTheCountKeepTheirSettings) {
    DrumKitModule kit;
    EXPECT_TRUE(kit.getExtraState().isVoid()) << "an untouched kit saves no state";
    kit.setPadCount(16);
    auto pad = kit.pad(13);
    pad.note = 70;
    pad.name = "Hi-mid tom high";
    pad.shown = false;
    ASSERT_TRUE(kit.setPad(13, pad));
    kit.setPadCount(8);
    const auto state = kit.getExtraState();
    ASSERT_TRUE(state.isObject());

    DrumKitModule back;
    back.setExtraState(juce::JSON::parse(juce::JSON::toString(state)));
    EXPECT_EQ(back.padCount(), 8);
    EXPECT_EQ(back.pad(13).note, 70);
    EXPECT_EQ(back.pad(13).name, "Hi-mid tom high");
    EXPECT_FALSE(back.pad(13).shown);
    EXPECT_EQ(back.pad(10).sound, 41);
    EXPECT_TRUE(back.padTable().pads == kit.padTable().pads);

    back.setExtraState({});
    EXPECT_EQ(back.padCount(), 8) << "no pad state is a new kit's pads";
    EXPECT_EQ(back.pad(13).note, 48);
}

TEST(DrumKitPads, AStateIsClampedOnTheWayIn) {
    auto state = juce::JSON::parse(R"({"padCount": 99, "pads": [{"kind": "file", "sound": 300, "note": -5}]})");
    DrumKitModule kit;
    kit.setExtraState(state);
    EXPECT_EQ(kit.padCount(), 32);
    EXPECT_EQ(kit.pad(0).note, 0);
    EXPECT_EQ(kit.pad(0).sound, 127);
    EXPECT_EQ(kit.pad(0).kind, PadKind::BuiltIn) << "a file pad with no file plays its drum";
}

TEST(DrumKitPads, AGroupParameterWritesEveryPadOfItsGroupAndTheHatDecaysSplit) {
    DrumKitModule kit;
    setActual(kit, "kickLevel", 0.33f);
    EXPECT_NEAR(actual(kit, 0, PadParam::Level), 0.33f, 1.0e-5f);
    EXPECT_NEAR(actual(kit, 16, PadParam::Level), 0.33f, 1.0e-5f) << "Kick 2";
    EXPECT_NEAR(actual(kit, 1, PadParam::Level), 0.8f, 1.0e-5f) << "the snare is not a kick";

    setActual(kit, "snareTune", -5.0f);
    for (int pad : {1, 8, 17}) // snare, rim, snare 2
        EXPECT_NEAR(actual(kit, pad, PadParam::Tune), -5.0f, 1.0e-4f) << pad;

    setActual(kit, "closedDecay", 0.2f);
    setActual(kit, "openDecay", 1.2f);
    EXPECT_NEAR(actual(kit, 3, PadParam::Decay), 0.2f, 1.0e-4f) << "closed hat";
    EXPECT_NEAR(actual(kit, 9, PadParam::Decay), 0.2f, 1.0e-4f) << "pedal hat";
    EXPECT_NEAR(actual(kit, 4, PadParam::Decay), 1.2f, 1.0e-4f) << "open hat";

    setActual(kit, "hatLevel", 0.1f);
    for (int pad : {3, 4, 9})
        EXPECT_NEAR(actual(kit, pad, PadParam::Level), 0.1f, 1.0e-5f) << pad;

    // A pad set apart keeps its value until its group changes again.
    kit.padParameter(0, PadParam::Level)->setValueNotifyingHost(0.9f);
    setActual(kit, "kickLevel", 0.33f);
    EXPECT_NEAR(actual(kit, 0, PadParam::Level), 0.9f, 1.0e-5f) << "restating the same group value";
    setActual(kit, "kickLevel", 0.5f);
    EXPECT_NEAR(actual(kit, 0, PadParam::Level), 0.5f, 1.0e-5f);
}

TEST(DrumKitPads, AutomationOfAGroupParameterReachesItsPadsByTheNextBlock) {
    auto kit = makeKit();
    auto* tomTune = static_cast<juce::AudioParameterFloat*>(findParameterByID(kit.get(), "tomTune"));
    static_cast<juce::AudioProcessorParameter*>(tomTune)->setValue(
        tomTune->convertTo0to1(4.0f)); // what AutomationApplier does: no listener is told
    render(*kit, {}, 0.01);
    for (int pad : {5, 10, 11, 12, 13, 14})
        EXPECT_NEAR(actual(*kit, pad, PadParam::Tune), 4.0f, 1.0e-4f) << pad;
}

TEST(DrumKitPads, TwoPadsOnOneDrumBothSound) {
    // Pad 1 (36) and Kick 2 (35) are both kicks: the old kit had one kick voice, so the second hit cut the first.
    // Here the first kick's tail plays on under the second: taking the second away leaves the first, and the two
    // can partly cancel, so the test compares the tail rather than the loudness.
    auto both = makeKit();
    const auto twoKicks = render(*both, {{36, 1.0f, 0}, {35, 1.0f, 2000}}, 0.4);
    auto one = makeKit();
    const auto oneKick = render(*one, {{35, 1.0f, 2000}}, 0.4);
    auto first = makeKit();
    const auto firstKick = render(*first, {{36, 1.0f, 0}}, 0.4);
    const float tail = rmsOf(firstKick, 0.06, 0.2);
    ASSERT_GT(tail, 1.0e-3f);
    EXPECT_NEAR(rmsOf(minus(twoKicks, oneKick), 0.06, 0.2), tail, tail * 0.2f);

    // Two pads made to play the same snare sound, on different notes, also both sound.
    auto kit = makeKit();
    auto pad = kit->pad(20);
    pad.sound = 38;
    pad.kind = PadKind::BuiltIn;
    ASSERT_TRUE(kit->setPad(20, pad));
    setActual(*kit, "snareDecay", 0.5f);
    const auto twoSnares = render(*kit, {{38, 1.0f, 0}, {52, 1.0f, 3000}}, 0.4);
    auto lone = makeKit();
    setActual(*lone, "snareDecay", 0.5f);
    const auto oneSnare = render(*lone, {{38, 1.0f, 3000}}, 0.4);
    EXPECT_GT(rmsOf(minus(twoSnares, oneSnare), 0.1, 0.3), rmsOf(oneSnare, 0.1, 0.3) * 0.3f)
        << "the pad 21 snare sounds as well";
}

TEST(DrumKitPads, AClosedOrPedalHatChokesEveryOpenHatPad) {
    const auto withSecondOpenHat = [] {
        auto kit = makeKit();
        setActual(*kit, "openDecay", 1.5f);
        auto pad = kit->pad(20);
        pad.sound = 46;
        pad.kind = PadKind::BuiltIn;
        EXPECT_TRUE(kit->setPad(20, pad));
        return kit;
    };
    auto ringing = withSecondOpenHat();
    const auto open = render(*ringing, {{46, 1.0f, 0}, {52, 1.0f, 0}}, 0.6);
    auto choked = withSecondOpenHat();
    const auto cut = render(*choked, {{46, 1.0f, 0}, {52, 1.0f, 0}, {44, 0.1f, 4410}}, 0.6);
    EXPECT_GT(rmsOf(open, 0.3, 0.6), 1.0e-3f);
    EXPECT_LT(rmsOf(cut, 0.3, 0.6), rmsOf(open, 0.3, 0.6) * 0.01f) << "both open hats stop";
}

TEST(DrumKitPads, AFilePadPlaysFromItsStartAtTheNoteSample) {
    TempDir dir;
    const auto wav = dir.file("half.wav");
    // Silence for the first half, then a constant 0.5.
    ASSERT_TRUE(writeWav(wav, 8820, 1, kRate, [](int, int frame) { return frame < 4410 ? 0.0f : 0.5f; }));

    auto kit = makeKit();
    ASSERT_TRUE(kit->setPadFile(20, wav));
    EXPECT_EQ(kit->pad(20).kind, PadKind::File);
    EXPECT_EQ(kit->pad(20).name, "half");
    kit->padParameter(20, PadParam::Start)->setValueNotifyingHost(0.5f);
    const auto fromMiddle = render(*kit, {{52, 1.0f, 300}}, 0.05);
    EXPECT_EQ(fromMiddle[299], 0.0f);
    EXPECT_GT(std::fabs(fromMiddle[302]), 0.1f) << "the onset is at the note's sample";
    EXPECT_NEAR(fromMiddle[1000], 0.5f * 0.8f * 0.7f, 0.01f) << "file x pad level x master level";

    auto fromTop = makeKit();
    ASSERT_TRUE(fromTop->setPadFile(20, wav));
    const auto fromStart = render(*fromTop, {{52, 1.0f, 300}}, 0.2);
    EXPECT_LT(rmsOf(fromStart, 0.0, 4400.0 / kRate), 1.0e-6f) << "Start 0 plays the silent half first";
    EXPECT_GT(rmsOf(fromStart, 5000.0 / kRate, 0.15), 0.1f);

    EXPECT_FALSE(kit->setPadFile(21, dir.file("missing.wav")));
    EXPECT_EQ(kit->pad(21).kind, PadKind::BuiltIn);
}

TEST(DrumKitPads, AFilePadIsSavedAsAPathTheProjectCanRewrite) {
    TempDir dir;
    const auto wav = dir.file("clap.wav");
    ASSERT_TRUE(writeWav(wav, 4410, 1, kRate, [](int, int) { return 0.3f; }));
    DrumKitModule kit;
    ASSERT_TRUE(kit.setPadFile(2, wav));
    const auto state = kit.getExtraState();
    const auto* files = state.getProperty("padFiles", {}).getArray();
    ASSERT_NE(files, nullptr);
    ASSERT_EQ(files->size(), 32);
    EXPECT_EQ((*files)[2].toString(), wav.getFullPathName());
    EXPECT_TRUE((*files)[0].toString().isEmpty());

    auto back = makeKit();
    back->setExtraState(state);
    EXPECT_EQ(back->pad(2).kind, PadKind::File);
    EXPECT_GT(rmsOf(render(*back, {{39}}, 0.05), 0.0, 0.05), 0.05f) << "the file plays after a restore";
}

TEST(DrumKitPads, ARecordedSwitchMovesItsGroupsPadsAndAKitPickFillsEveryPad) {
    DrumKitModule kit;
    setActual(kit, "tomRecorded", 0.0f);
    for (int pad : {5, 10, 11, 12, 13, 14})
        EXPECT_EQ(kit.pad(pad).kind, PadKind::BuiltIn) << pad;
    EXPECT_EQ(kit.pad(0).kind, PadKind::Kit);
    setActual(kit, "tomRecorded", 1.0f);
    EXPECT_EQ(kit.pad(5).kind, PadKind::Kit);

    setActual(kit, "snareRecorded", 0.0f);
    kit.fillPadsWithKit();
    for (int i = 0; i < synth::drums::kMaxPads; ++i)
        EXPECT_EQ(kit.pad(i).kind, PadKind::Kit) << i;
    EXPECT_TRUE(dynamic_cast<juce::AudioParameterBool*>(findParameterByID(&kit, "snareRecorded"))->get());
}

TEST(DrumKitPads, AnAuditionPlaysThePadAtTheNextBlock) {
    auto kit = makeKit();
    kit->auditionPad(1);
    const auto out = render(*kit, {}, 0.1);
    EXPECT_GT(rmsOf(out, 0.0, 0.1), 1.0e-3f);
    EXPECT_EQ(kit->lastPlayedPad(), 1);
}

TEST(DrumKitPads, OnlyThePadParametersAreRecognisedAsPadIds) {
    EXPECT_TRUE(DrumKitModule::isPadParamId("pad1Level"));
    EXPECT_TRUE(DrumKitModule::isPadParamId("pad32Start"));
    EXPECT_FALSE(DrumKitModule::isPadParamId("pad33Level"));
    EXPECT_FALSE(DrumKitModule::isPadParamId("pad0Tune"));
    EXPECT_FALSE(DrumKitModule::isPadParamId("padLevel"));
    EXPECT_FALSE(DrumKitModule::isPadParamId("kickLevel"));
}

TEST(DrumKitPads, TheModelNeverSeesThePadParameters) {
    const auto schema = synth::AIStateMapper::getModuleSchema();
    EXPECT_TRUE(schema.contains("kickDecay"));
    EXPECT_FALSE(schema.contains("pad1Level"));
}

// Regression test for FRO1304: Shown only decides what the card and the clip editors draw; a hidden pad keeps its
// sound and its note, so its hits still play, and the switch alone changes no sample.
TEST(DrumKitPads, AHiddenPadStillPlaysItsHits) {
    auto shownKit = makeKit();
    auto hiddenKit = makeKit();
    ASSERT_TRUE(hiddenKit->setPadShown(6, false)); // the cymbal
    EXPECT_FALSE(hiddenKit->padShown(6));
    const auto expected = render(*shownKit, {{49}, {36, 1.0f, 4000}}, 0.5);
    const auto actual = render(*hiddenKit, {{49}, {36, 1.0f, 4000}}, 0.5);
    ASSERT_EQ(expected.size(), actual.size());
    EXPECT_GT(rmsOf(actual, 0.0, 0.1), 1.0e-3f) << "the hidden cymbal sounds";
    for (size_t i = 0; i < expected.size(); ++i)
        ASSERT_EQ(expected[i], actual[i]) << "sample " << i;
}

TEST(DrumKitPads, ShownIsPerPadAndShowAllReachesEveryPad) {
    DrumKitModule kit;
    EXPECT_FALSE(kit.setPadShown(2, true)) << "already shown";
    EXPECT_TRUE(kit.setPadShown(2, false));
    EXPECT_TRUE(kit.setPadShown(12, false)); // beyond the 8 the card draws
    EXPECT_EQ(kit.hiddenPadCount(), 1) << "only the pads within the count are counted";
    EXPECT_EQ((int)kit.shownPads().size(), 7);
    EXPECT_EQ(kit.shownPads()[2], 3) << "the pads after a hidden one keep their numbers";
    EXPECT_FALSE(kit.getExtraState().isVoid()) << "a hidden pad is saved";
    EXPECT_EQ(kit.showAllPads(), 2);
    EXPECT_EQ(kit.hiddenPadCount(), 0);
    EXPECT_TRUE(kit.getExtraState().isVoid()) << "all shown is an untouched kit again";
}
