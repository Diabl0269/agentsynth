// LFOSyncPhaseLockTests.cpp -- the LFO's longer sync rates, the song-locked phase, and the
// plugin-host state compatibility the longer list needs.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/LFOModule.h"

#include <cmath>
#include <gtest/gtest.h>
#include <vector>

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kBlock = 512;

class FakePlayHead : public juce::AudioPlayHead {
public:
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo info;
        info.setBpm(120.0);
        info.setIsPlaying(playing);
        info.setPpqPosition(ppq);
        return info;
    }
    bool playing = true;
    double ppq = 0.0;
};

juce::AudioParameterChoice* choice(LFOModule& m, const char* id) {
    return dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(&m, id));
}
juce::AudioParameterBool* boolean(LFOModule& m, const char* id) {
    return dynamic_cast<juce::AudioParameterBool*>(findParameterByID(&m, id));
}

struct Rig {
    LFOModule lfo;
    FakePlayHead head;
    Rig() {
        lfo.setPlayHead(&head);
        lfo.prepareToPlay(kSampleRate, kBlock);
        *boolean(lfo, "mode") = true;
        *choice(lfo, "shape") = 2;    // Sawtooth: -1 at phase 0, 0 at phase 0.5
        *choice(lfo, "rateSync") = 7; // 1/1 = one 4/4 bar
    }
    std::vector<float> block() {
        juce::AudioBuffer<float> buf(LFOModule::kNumInputs, kBlock);
        buf.clear();
        juce::MidiBuffer midi;
        lfo.processBlock(buf, midi);
        return {buf.getReadPointer(0), buf.getReadPointer(0) + kBlock};
    }
};

juce::MemoryBlock blobWith(const char* id, float normalised, const char* name = nullptr) {
    juce::ValueTree state("ModuleState");
    state.setProperty(id, normalised, nullptr);
    if (name != nullptr)
        state.setProperty(juce::String(id) + "__name", juce::String(name), nullptr);
    juce::MemoryBlock block;
    juce::AudioProcessor::copyXmlToBinary(*state.createXml(), block);
    return block;
}

} // namespace

TEST(LFOSyncPhaseLock, BarLineStartsTheCycle) {
    Rig rig;
    rig.head.ppq = 4.0;
    const auto out = rig.block();
    EXPECT_NEAR(out[0], -1.0f, 1e-4f) << "ppq 4 is a bar line, so a 1/1 saw is at phase 0";
}

TEST(LFOSyncPhaseLock, PhaseOffsetStillAddsOnTop) {
    Rig rig;
    auto* phase = dynamic_cast<juce::RangedAudioParameter*>(findParameterByID(&rig.lfo, "phase"));
    phase->setValueNotifyingHost(phase->convertTo0to1(180.0f));
    rig.head.ppq = 8.0;
    EXPECT_NEAR(rig.block()[0], 0.0f, 1e-4f);
}

TEST(LFOSyncPhaseLock, HalfwayThroughTheBarIsHalfwayThroughTheCycle) {
    Rig rig;
    rig.head.ppq = 6.0;
    EXPECT_NEAR(rig.block()[0], 0.0f, 1e-4f);
}

TEST(LFOSyncPhaseLock, RunsStartedOnDifferentBarsAreIdentical) {
    Rig a;
    a.head.ppq = 4.0;
    Rig b;
    b.head.ppq = 36.0;
    EXPECT_EQ(a.block(), b.block());
}

TEST(LFOSyncPhaseLock, LongCycleLocksToItsOwnLength) {
    Rig rig;
    *choice(rig.lfo, "rateSync") = 10; // 8/1 = 32 beats
    rig.head.ppq = 32.0;
    EXPECT_NEAR(rig.block()[0], -1.0f, 1e-4f);
    rig.head.ppq = 48.0; // halfway
    EXPECT_NEAR(rig.block()[0], 0.0f, 1e-4f);
}

TEST(LFOSyncPhaseLock, StoppedTransportKeepsFreeRunning) {
    Rig stopped;
    stopped.head.playing = false;
    stopped.head.ppq = 6.0;
    Rig reference;
    reference.lfo.setPlayHead(nullptr);
    EXPECT_EQ(stopped.block(), reference.block());
    EXPECT_EQ(stopped.block(), reference.block());
}

TEST(LFOSyncPhaseLock, RetrigOnKeepsFreeRunning) {
    Rig retrig;
    *boolean(retrig.lfo, "retrig") = true;
    retrig.head.ppq = 6.0;
    Rig reference;
    *boolean(reference.lfo, "retrig") = true;
    reference.lfo.setPlayHead(nullptr);
    EXPECT_EQ(retrig.block(), reference.block());
}

TEST(LFOSyncPhaseLock, FreeHzModeKeepsFreeRunning) {
    Rig hz;
    *boolean(hz.lfo, "mode") = false;
    hz.head.ppq = 6.0;
    Rig reference;
    *boolean(reference.lfo, "mode") = false;
    reference.lfo.setPlayHead(nullptr);
    EXPECT_EQ(hz.block(), reference.block());
}

TEST(LFORateCompat, ProjectsSavedByNameStillLoadTheSameRate) {
    for (const char* name : {"1/4", "1/1", "8/1"}) {
        LFOModule lfo;
        auto* obj = new juce::DynamicObject();
        obj->setProperty("rateSync", juce::String(name));
        juce::var keep(obj);
        synth::AIStateMapper::applyUntrustedParams(&lfo, obj);
        EXPECT_EQ(choice(lfo, "rateSync")->getCurrentChoiceName(), name);
    }
}

TEST(LFORateCompat, LegacyPluginBlobDecodesOverTheOldEightChoices) {
    LFOModule lfo;
    const auto blob = blobWith("rateSync", 5.0f / 7.0f); // "1/4" when there were 8 choices
    lfo.setStateInformation(blob.getData(), (int)blob.getSize());
    EXPECT_EQ(choice(lfo, "rateSync")->getCurrentChoiceName(), "1/4");

    LFOModule top;
    const auto topBlob = blobWith("rateSync", 1.0f); // "1/1", the old last entry
    top.setStateInformation(topBlob.getData(), (int)topBlob.getSize());
    EXPECT_EQ(choice(top, "rateSync")->getCurrentChoiceName(), "1/1");
}

TEST(LFORateCompat, NewPluginBlobRoundTripsTheLongRates) {
    for (int index : {5, 8, 10}) {
        LFOModule a;
        *choice(a, "rateSync") = index;
        juce::MemoryBlock blob;
        a.getStateInformation(blob);
        LFOModule b;
        b.setStateInformation(blob.getData(), (int)blob.getSize());
        EXPECT_EQ(choice(b, "rateSync")->getIndex(), index);
    }
}

TEST(LFORateCompat, SavedNameWinsOverTheNormalisedValue) {
    LFOModule lfo;
    const auto blob = blobWith("rateSync", 0.0f, "8/1");
    lfo.setStateInformation(blob.getData(), (int)blob.getSize());
    EXPECT_EQ(choice(lfo, "rateSync")->getCurrentChoiceName(), "8/1");
}
