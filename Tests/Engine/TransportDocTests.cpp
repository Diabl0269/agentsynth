// Concern: TransportDoc's JSON form and TransportService's message-thread mirror of the requested
// tempo, time signature and loop.
#include "Transport/TransportDoc.h"
#include "Transport/TransportService.h"
#include <gtest/gtest.h>

using synth::TransportDoc;
using synth::TransportService;

namespace {

TransportDoc customDoc() {
    TransportDoc t;
    t.bpm = 96.5;
    t.timeSigNumerator = 7;
    t.timeSigDenominator = 8;
    t.loopStartBeat = 2.0;
    t.loopEndBeat = 10.5;
    t.loopEnabled = true;
    return t;
}

bool rejects(const juce::var& v) {
    TransportDoc target = customDoc();
    const bool ok = target.fromVar(v);
    EXPECT_EQ(target, customDoc()) << "a failed fromVar must leave the target untouched";
    return !ok;
}

juce::var with(const char* key, const juce::var& value) {
    auto v = TransportDoc{}.toVar();
    v.getDynamicObject()->setProperty(key, value);
    return v;
}

} // namespace

TEST(TransportDocTest, DefaultsMatchTheAudioSideDefaults) {
    const TransportDoc d;
    EXPECT_DOUBLE_EQ(d.bpm, 120.0);
    EXPECT_EQ(d.timeSigNumerator, 4);
    EXPECT_EQ(d.timeSigDenominator, 4);
    EXPECT_DOUBLE_EQ(d.loopStartBeat, 0.0);
    EXPECT_DOUBLE_EQ(d.loopEndBeat, 4.0);
    EXPECT_FALSE(d.loopEnabled);
}

TEST(TransportDocTest, RoundTripsThroughVar) {
    TransportDoc out;
    ASSERT_TRUE(out.fromVar(customDoc().toVar()));
    EXPECT_EQ(out, customDoc());
    EXPECT_NE(out, TransportDoc{});
}

TEST(TransportDocTest, RoundTripsThroughJsonText) {
    const auto text = juce::JSON::toString(customDoc().toVar());
    TransportDoc out;
    ASSERT_TRUE(out.fromVar(juce::JSON::parse(text)));
    EXPECT_EQ(out, customDoc());
}

TEST(TransportDocTest, RejectsNonObjectsAndMissingKeys) {
    EXPECT_TRUE(rejects(juce::var()));
    EXPECT_TRUE(rejects(juce::var("x")));
    for (const char* key : {"bpm", "timeSigNum", "timeSigDen", "loopStartBeat", "loopEndBeat", "loopEnabled"}) {
        auto v = TransportDoc{}.toVar();
        v.getDynamicObject()->removeProperty(key);
        EXPECT_TRUE(rejects(v)) << key;
    }
}

TEST(TransportDocTest, RejectsWrongTypes) {
    EXPECT_TRUE(rejects(with("bpm", "120")));
    EXPECT_TRUE(rejects(with("timeSigNum", "4")));
    EXPECT_TRUE(rejects(with("loopEnabled", 1)));
    EXPECT_TRUE(rejects(with("loopStartBeat", true)));
    EXPECT_TRUE(rejects(with("timeSigNum", 4.5)));
}

TEST(TransportDocTest, RejectsOutOfRangeValues) {
    EXPECT_TRUE(rejects(with("bpm", 4.0)));
    EXPECT_TRUE(rejects(with("bpm", 991.0)));
    EXPECT_TRUE(rejects(with("bpm", std::numeric_limits<double>::infinity())));
    EXPECT_TRUE(rejects(with("bpm", std::numeric_limits<double>::quiet_NaN())));
    EXPECT_TRUE(rejects(with("timeSigNum", 0)));
    EXPECT_TRUE(rejects(with("timeSigNum", 65)));
    EXPECT_TRUE(rejects(with("timeSigDen", 3)));
    EXPECT_TRUE(rejects(with("timeSigDen", 64)));
    EXPECT_TRUE(rejects(with("loopStartBeat", -1.0)));
    EXPECT_TRUE(rejects(with("loopEndBeat", -1.0)));
}

TEST(TransportDocTest, EnabledLoopMustBeLongEnoughButDisabledMayBeAnything) {
    auto tooShort = with("loopEndBeat", 4.0);
    tooShort.getDynamicObject()->setProperty("loopStartBeat", 4.0);
    tooShort.getDynamicObject()->setProperty("loopEnabled", true);
    EXPECT_TRUE(rejects(tooShort));

    tooShort.getDynamicObject()->setProperty("loopEnabled", false);
    TransportDoc out;
    EXPECT_TRUE(out.fromVar(tooShort));
}

TEST(TransportDocTest, AcceptsBoundaryValues) {
    TransportDoc out;
    auto v = TransportDoc{}.toVar();
    auto* obj = v.getDynamicObject();
    obj->setProperty("bpm", TransportService::kMinBpm);
    obj->setProperty("timeSigNum", 64);
    obj->setProperty("timeSigDen", 32);
    EXPECT_TRUE(out.fromVar(v));
    obj->setProperty("bpm", TransportService::kMaxBpm);
    obj->setProperty("timeSigNum", 1);
    obj->setProperty("timeSigDen", 1);
    EXPECT_TRUE(out.fromVar(v));
}

// ---- TransportService mirror ----------------------------------------------------------------------

TEST(TransportServiceMirrorTest, StartsAtTheAudioSideDefaults) {
    TransportService t;
    EXPECT_EQ(t.getDocumentState(), TransportDoc{});
    const auto snap = t.getPositionSnapshot();
    EXPECT_DOUBLE_EQ(snap.bpm, t.getDocumentState().bpm);
    EXPECT_EQ(snap.timeSigNumerator, t.getDocumentState().timeSigNumerator);
}

TEST(TransportServiceMirrorTest, ReflectsSettersImmediatelyWithoutATick) {
    TransportService t;
    ASSERT_TRUE(t.setBpm(96.0));
    ASSERT_TRUE(t.setTimeSignature(3, 4));
    ASSERT_TRUE(t.setLoop(4.0, 16.0, true));

    const auto& d = t.getDocumentState();
    EXPECT_DOUBLE_EQ(d.bpm, 96.0);
    EXPECT_EQ(d.timeSigNumerator, 3);
    EXPECT_EQ(d.timeSigDenominator, 4);
    EXPECT_DOUBLE_EQ(d.loopStartBeat, 4.0);
    EXPECT_DOUBLE_EQ(d.loopEndBeat, 16.0);
    EXPECT_TRUE(d.loopEnabled);
    // The audio-side snapshot has not moved: that is the reason the mirror exists.
    EXPECT_DOUBLE_EQ(t.getPositionSnapshot().bpm, 120.0);
}

TEST(TransportServiceMirrorTest, StoresTheClampedBpm) {
    TransportService t;
    ASSERT_TRUE(t.setBpm(5000.0));
    EXPECT_DOUBLE_EQ(t.getDocumentState().bpm, TransportService::kMaxBpm);
    ASSERT_TRUE(t.setBpm(1.0));
    EXPECT_DOUBLE_EQ(t.getDocumentState().bpm, TransportService::kMinBpm);
}

TEST(TransportServiceMirrorTest, LoopStoresClampedBoundsAndComputedFlag) {
    TransportService t;
    ASSERT_TRUE(t.setLoop(-3.0, 8.0, true));
    EXPECT_DOUBLE_EQ(t.getDocumentState().loopStartBeat, 0.0);
    EXPECT_DOUBLE_EQ(t.getDocumentState().loopEndBeat, 8.0);
    EXPECT_TRUE(t.getDocumentState().loopEnabled);

    // Too short to loop: the request to enable is stored as disabled, exactly what the audio side does.
    ASSERT_TRUE(t.setLoop(2.0, 2.0, true));
    EXPECT_FALSE(t.getDocumentState().loopEnabled);
}

TEST(TransportServiceMirrorTest, RejectedTimeSignatureLeavesTheMirrorAlone) {
    TransportService t;
    ASSERT_TRUE(t.setTimeSignature(7, 8));
    EXPECT_FALSE(t.setTimeSignature(0, 4));
    EXPECT_FALSE(t.setTimeSignature(4, 3));
    EXPECT_EQ(t.getDocumentState().timeSigNumerator, 7);
    EXPECT_EQ(t.getDocumentState().timeSigDenominator, 8);
}

TEST(TransportServiceMirrorTest, DroppedCommandLeavesTheMirrorAlone) {
    TransportService t;
    // Nothing drains the FIFO (no tick), so it fills up; a dropped command must not reach the mirror.
    int accepted = 0;
    while (t.locateBeat(1.0))
        ++accepted;
    ASSERT_GT(accepted, 0);
    EXPECT_FALSE(t.setBpm(90.0));
    EXPECT_DOUBLE_EQ(t.getDocumentState().bpm, 120.0);
}

TEST(TransportServiceMirrorTest, ApplyDocumentStateAppliesAllThreeAndReachesTheAudioSide) {
    TransportService t;
    ASSERT_TRUE(t.applyDocumentState(customDoc()));
    EXPECT_EQ(t.getDocumentState(), customDoc());

    t.prepare(48000.0, 512);
    t.tick(512);
    const auto snap = t.getPositionSnapshot();
    EXPECT_DOUBLE_EQ(snap.bpm, 96.5);
    EXPECT_EQ(snap.timeSigNumerator, 7);
    EXPECT_EQ(snap.timeSigDenominator, 8);
    EXPECT_TRUE(snap.looping);
    EXPECT_DOUBLE_EQ(snap.loopStartPpq, 2.0);
    EXPECT_DOUBLE_EQ(snap.loopEndPpq, 10.5);
}
