// Concern: FRO277's jump-to-next/previous-marker transport actions (transportJumpToNextMarker/
// transportJumpToPreviousMarker). Three layers, mirroring ShortcutManagerCursorActionsTests.cpp:
// table shape against a bare ShortcutManager (ids, labels, unbound, command mapping), the pure
// search arithmetic in Transport/MarkerJump.h against a bare list of beats, and invokeDirectly
// dispatch against a real MainComponent + AudioEngine with markers in its live TimelineDoc.
#include "AI/AIProvider.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "ShortcutManagerTestFixture.h"
#include "Transport/MarkerJump.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/ThemeManager.h"
#include <gtest/gtest.h>

namespace {

struct MarkerAction {
    const char* id;
    const char* label;
    juce::CommandID command;
};

const std::vector<MarkerAction>& markerActions() {
    static const std::vector<MarkerAction> actions{
        {"transportJumpToNextMarker", "Jump to Next Marker", AppCommands::transportJumpToNextMarker},
        {"transportJumpToPreviousMarker", "Jump to Previous Marker", AppCommands::transportJumpToPreviousMarker},
    };
    return actions;
}

class MarkerTestProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MarkerTestMock"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        if (callback)
            callback({}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        if (callback)
            callback(AIResponse{false, {}, {}, {}});
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String&) override {}
    juce::String getCurrentModel() const override { return {}; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    int requestTimeoutMs = 240000;
};

// A bare transport, prepared and ticked once so posted commands have been applied.
struct BareTransport {
    BareTransport() {
        transport.prepare(44100.0, 512);
        settle();
    }
    void settle() { transport.tick(512); }
    double ppq() const { return transport.getPositionSnapshot().ppq; }
    synth::TransportService transport;
    synth::TransportNudgeState state;
};

// A real app around a hosted engine; settle() runs one audio block so posted commands apply.
struct MarkerApp {
    MarkerApp()
        : engine(AudioEngine::HostMode::Hosted) {
        engine.initialise();
        engine.prepareForHost(44100.0, 512, 0, 2);
        mc = std::make_unique<MainComponent>(tm, lf, engine, std::make_unique<MarkerTestProvider>());
        settle();
    }
    void settle() { engine.processHostBlock(buffer, midi); }
    bool invoke(juce::CommandID id) { return mc->getCommandManager().invokeDirectly(id, false); }
    double ppq() { return engine.getTransport().getPositionSnapshot().ppq; }

    synth::theme::ThemeManager tm;
    synth::theme::AppLookAndFeel lf;
    AudioEngine engine;
    std::unique_ptr<MainComponent> mc;
    juce::AudioBuffer<float> buffer{2, 512};
    juce::MidiBuffer midi;
};

} // namespace

// ============================================================================
// Table shape
// ============================================================================

TEST_F(ShortcutManagerTest, MarkerActionsAreRegisteredUnboundGeneralWithTheAgreedLabels) {
    for (const auto& a : markerActions()) {
        EXPECT_TRUE(manager.getActionIds().contains(a.id)) << a.id;
        EXPECT_FALSE(manager.getBinding(a.id).isValid()) << a.id << " ships unbound";
        EXPECT_EQ(ShortcutManager::getActionDescription(a.id), juce::String(a.label));
        EXPECT_EQ(ShortcutManager::getCategory(a.id), ShortcutCategory::General) << a.id;
        EXPECT_TRUE(ShortcutManager::getActionIdsInCategory(ShortcutCategory::General).contains(a.id)) << a.id;
    }
}

TEST_F(ShortcutManagerTest, MarkerActionsRoundTripThroughTheirCommandId) {
    for (const auto& a : markerActions()) {
        const auto command = AppCommands::getCommandForAction(a.id);
        EXPECT_EQ(command, a.command) << a.id;
        EXPECT_NE(command, AppCommands::kNoCommand) << a.id;
    }
}

// ============================================================================
// Search arithmetic (pure, headless)
// ============================================================================

TEST(MarkerJump, NextFindsTheNearestMarkerStrictlyAheadUnsortedInputAllowed) {
    const std::vector<double> beats{10.0, 2.0, 6.0};
    EXPECT_DOUBLE_EQ(*synth::findAdjacentMarkerBeat(beats, 0.0, true), 2.0) << "before the first marker";
    EXPECT_DOUBLE_EQ(*synth::findAdjacentMarkerBeat(beats, 2.0, true), 6.0)
        << "sitting exactly on a marker steps past it";
    EXPECT_DOUBLE_EQ(*synth::findAdjacentMarkerBeat(beats, 4.0, true), 6.0) << "between markers";
    EXPECT_FALSE(synth::findAdjacentMarkerBeat(beats, 10.0, true)) << "at the last marker: no-op, never wraps";
    EXPECT_FALSE(synth::findAdjacentMarkerBeat(beats, 11.0, true)) << "after the last marker: no-op";
}

TEST(MarkerJump, PreviousFindsTheNearestMarkerStrictlyBehindUnsortedInputAllowed) {
    const std::vector<double> beats{10.0, 2.0, 6.0};
    EXPECT_DOUBLE_EQ(*synth::findAdjacentMarkerBeat(beats, 12.0, false), 10.0) << "after the last marker";
    EXPECT_DOUBLE_EQ(*synth::findAdjacentMarkerBeat(beats, 10.0, false), 6.0)
        << "sitting exactly on a marker steps past it";
    EXPECT_DOUBLE_EQ(*synth::findAdjacentMarkerBeat(beats, 8.0, false), 6.0) << "between markers";
    EXPECT_FALSE(synth::findAdjacentMarkerBeat(beats, 2.0, false)) << "at the first marker: no-op, never wraps";
    EXPECT_FALSE(synth::findAdjacentMarkerBeat(beats, 1.0, false)) << "before the first marker: no-op";
}

TEST(MarkerJump, EmptyMarkerListIsAlwaysANoOp) {
    const std::vector<double> beats;
    EXPECT_FALSE(synth::findAdjacentMarkerBeat(beats, 0.0, true));
    EXPECT_FALSE(synth::findAdjacentMarkerBeat(beats, 0.0, false));
}

// ============================================================================
// jumpToAdjacentMarker against a bare TransportService
// ============================================================================

TEST(MarkerJump, JumpsToTheNextAndPreviousMarkerFromTheCurrentPosition) {
    BareTransport t;
    const std::vector<double> beats{2.0, 6.0, 10.0};
    t.transport.locateBeat(4.0);
    t.settle();
    EXPECT_TRUE(synth::jumpToAdjacentMarker(t.transport, t.state, beats, true));
    t.settle();
    EXPECT_DOUBLE_EQ(t.ppq(), 6.0);
    EXPECT_TRUE(synth::jumpToAdjacentMarker(t.transport, t.state, beats, false));
    t.settle();
    EXPECT_DOUBLE_EQ(t.ppq(), 2.0);
}

TEST(MarkerJump, PastTheLastMarkerNextIsANoOp) {
    BareTransport t;
    const std::vector<double> beats{2.0, 6.0};
    t.transport.locateBeat(6.0);
    t.settle();
    EXPECT_TRUE(synth::jumpToAdjacentMarker(t.transport, t.state, beats, true));
    t.settle();
    EXPECT_DOUBLE_EQ(t.ppq(), 6.0) << "no marker ahead -- never wraps to the first";
}

TEST(MarkerJump, BeforeTheFirstMarkerPreviousIsANoOp) {
    BareTransport t;
    const std::vector<double> beats{2.0, 6.0};
    t.transport.locateBeat(2.0);
    t.settle();
    EXPECT_TRUE(synth::jumpToAdjacentMarker(t.transport, t.state, beats, false));
    t.settle();
    EXPECT_DOUBLE_EQ(t.ppq(), 2.0) << "no marker behind -- never wraps to the last";
}

TEST(MarkerJump, RepeatedNextPressesFiredInsideOneBlockStepMarkerByMarker) {
    BareTransport t;
    const std::vector<double> beats{2.0, 6.0, 10.0, 14.0};
    synth::jumpToAdjacentMarker(t.transport, t.state, beats, true);
    synth::jumpToAdjacentMarker(t.transport, t.state, beats, true);
    synth::jumpToAdjacentMarker(t.transport, t.state, beats, true);
    t.settle();
    EXPECT_DOUBLE_EQ(t.ppq(), 10.0) << "three next presses before the audio thread applies the first must all count";
}

TEST(MarkerJump, ANoOpDirectionLeavesStateUntouchedSoTheOtherDirectionStillWorks) {
    BareTransport t;
    const std::vector<double> beats{2.0, 6.0};
    t.transport.locateBeat(6.0);
    t.settle();
    // Next is a no-op at the last marker; previous immediately afterward must still work from the
    // real position, not from some half-updated pending state left by the no-op.
    synth::jumpToAdjacentMarker(t.transport, t.state, beats, true);
    synth::jumpToAdjacentMarker(t.transport, t.state, beats, false);
    t.settle();
    EXPECT_DOUBLE_EQ(t.ppq(), 2.0);
}

TEST(MarkerJump, ANudgeAfterTheSnapshotMovedRecomputesFromTheSnapshot) {
    BareTransport t;
    const std::vector<double> beats{2.0, 6.0, 10.0};
    synth::jumpToAdjacentMarker(t.transport, t.state, beats, true);
    t.settle();
    EXPECT_DOUBLE_EQ(t.ppq(), 2.0);
    // Someone else relocates; the snapshot no longer matches what the pending request was based on.
    t.transport.locateBeat(7.0);
    t.settle();
    synth::jumpToAdjacentMarker(t.transport, t.state, beats, true);
    t.settle();
    EXPECT_DOUBLE_EQ(t.ppq(), 10.0);
}

// ============================================================================
// invokeDirectly dispatch through MainComponent, against a real TimelineDoc
// ============================================================================

TEST(ShortcutManagerMarkerActionsInvoke, CommandsJumpToTheNextAndPreviousMarkerInTheLiveTimelineDoc) {
    MarkerApp app;
    auto& doc = app.mc->getTimelineDoc();
    doc.addMarker(2.0, "Intro", 0xffffffff);
    doc.addMarker(10.0, "Chorus", 0xffffffff);
    doc.addMarker(6.0, "Verse", 0xffffffff);

    ASSERT_TRUE(app.invoke(AppCommands::transportJumpToNextMarker));
    app.settle();
    EXPECT_DOUBLE_EQ(app.ppq(), 2.0);
    ASSERT_TRUE(app.invoke(AppCommands::transportJumpToNextMarker));
    app.settle();
    EXPECT_DOUBLE_EQ(app.ppq(), 6.0);
    ASSERT_TRUE(app.invoke(AppCommands::transportJumpToNextMarker));
    app.settle();
    EXPECT_DOUBLE_EQ(app.ppq(), 10.0);
    ASSERT_TRUE(app.invoke(AppCommands::transportJumpToPreviousMarker));
    app.settle();
    EXPECT_DOUBLE_EQ(app.ppq(), 6.0);
}

TEST(ShortcutManagerMarkerActionsInvoke, NoMarkersMeansEveryJumpIsANoOp) {
    MarkerApp app;
    app.mc->getTimelineDoc().addMarker(5.0, "", 0xffffffff);
    app.mc->getTimelineDoc().removeMarker(app.mc->getTimelineDoc().getMarkers().front().id);
    ASSERT_TRUE(app.invoke(AppCommands::transportJumpToNextMarker));
    app.settle();
    EXPECT_DOUBLE_EQ(app.ppq(), 0.0);
    ASSERT_TRUE(app.invoke(AppCommands::transportJumpToPreviousMarker));
    app.settle();
    EXPECT_DOUBLE_EQ(app.ppq(), 0.0);
}

TEST(ShortcutManagerMarkerActionsInvoke, JumpWorksWhilePlayingWithoutStoppingTheTransport) {
    MarkerApp app;
    app.mc->getTimelineDoc().addMarker(4.0, "", 0xffffffff);
    auto& transport = app.engine.getTransport();
    ASSERT_TRUE(transport.play());
    app.settle();
    ASSERT_TRUE(transport.getPositionSnapshot().playing);
    ASSERT_TRUE(app.invoke(AppCommands::transportJumpToNextMarker));
    app.settle();
    EXPECT_NEAR(app.ppq(), 4.0, 1e-9);
    EXPECT_TRUE(transport.getPositionSnapshot().playing);
}

TEST(ShortcutManagerMarkerActionsInvoke, RapidNextCommandsWithinOneBlockStepMarkerByMarker) {
    MarkerApp app;
    auto& doc = app.mc->getTimelineDoc();
    doc.addMarker(2.0, "", 0xffffffff);
    doc.addMarker(6.0, "", 0xffffffff);
    doc.addMarker(10.0, "", 0xffffffff);

    ASSERT_TRUE(app.invoke(AppCommands::transportJumpToNextMarker));
    ASSERT_TRUE(app.invoke(AppCommands::transportJumpToNextMarker));
    app.settle();
    EXPECT_DOUBLE_EQ(app.ppq(), 6.0) << "two rapid presses before the audio thread applies the first must both count";
}
