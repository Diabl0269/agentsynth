// Concern: FRO271's cursor-move and loop-jump transport actions (transportNudgeBackBeat/ForwardBeat/
// BackBar/ForwardBar, transportJumpToLoopStart/End) and the "Play / Stop" label. Three layers: table
// shape against a bare ShortcutManager (ids, labels, unbound, command mapping), the pure nudge
// arithmetic in Transport/TransportNudge.h against a bare TransportService, and invokeDirectly
// dispatch against a real MainComponent + AudioEngine (mirrors ShortcutManagerTransportActionsTests.cpp).
#include "AI/AIProvider.h"
#include "AudioEngine/AudioEngine.h"
#include "MainComponent/MainComponent.h"
#include "ShortcutManagerTestFixture.h"
#include "Transport/TransportNudge.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/ThemeManager.h"
#include <gtest/gtest.h>

namespace {

struct CursorAction {
    const char* id;
    const char* label;
    juce::CommandID command;
};

const std::vector<CursorAction>& cursorActions() {
    static const std::vector<CursorAction> actions{
        {"transportNudgeBackBeat", "Move Cursor Back (Beat)", AppCommands::transportNudgeBackBeat},
        {"transportNudgeForwardBeat", "Move Cursor Forward (Beat)", AppCommands::transportNudgeForwardBeat},
        {"transportNudgeBackBar", "Move Cursor Back (Bar)", AppCommands::transportNudgeBackBar},
        {"transportNudgeForwardBar", "Move Cursor Forward (Bar)", AppCommands::transportNudgeForwardBar},
        {"transportJumpToLoopStart", "Jump to Loop Start", AppCommands::transportJumpToLoopStart},
        {"transportJumpToLoopEnd", "Jump to Loop End", AppCommands::transportJumpToLoopEnd},
    };
    return actions;
}

class CursorTestProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "CursorTestMock"; }
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
struct CursorApp {
    CursorApp()
        : engine(AudioEngine::HostMode::Hosted) {
        engine.initialise();
        engine.prepareForHost(44100.0, 512, 0, 2);
        mc = std::make_unique<MainComponent>(tm, lf, engine, std::make_unique<CursorTestProvider>());
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

TEST_F(ShortcutManagerTest, CursorActionsAreRegisteredUnboundGeneralWithTheAgreedLabels) {
    for (const auto& a : cursorActions()) {
        EXPECT_TRUE(manager.getActionIds().contains(a.id)) << a.id;
        EXPECT_FALSE(manager.getBinding(a.id).isValid()) << a.id << " ships unbound";
        EXPECT_EQ(ShortcutManager::getActionDescription(a.id), juce::String(a.label));
        EXPECT_EQ(ShortcutManager::getCategory(a.id), ShortcutCategory::General) << a.id;
        EXPECT_TRUE(ShortcutManager::getActionIdsInCategory(ShortcutCategory::General).contains(a.id)) << a.id;
    }
}

TEST_F(ShortcutManagerTest, CursorActionsRoundTripThroughTheirCommandId) {
    for (const auto& a : cursorActions()) {
        const auto command = AppCommands::getCommandForAction(a.id);
        EXPECT_EQ(command, a.command) << a.id;
        EXPECT_NE(command, AppCommands::kNoCommand) << a.id;
    }
}

TEST_F(ShortcutManagerTest, PlayStopLabelIsSharedByTheToggleAndItsAlias) {
    EXPECT_EQ(ShortcutManager::getActionDescription("togglePlayback"), juce::String("Play / Stop"));
    EXPECT_EQ(ShortcutManager::getActionDescription("transportTogglePlayStop"),
              ShortcutManager::getActionDescription("togglePlayback"));
}

// ============================================================================
// Nudge arithmetic against a bare TransportService
// ============================================================================

TEST(TransportNudge, BeatNudgesMoveByOneBeatBothWaysAndClampAtZero) {
    BareTransport t;
    synth::nudgeTransportCursor(t.transport, t.state, 1.0);
    t.settle();
    EXPECT_DOUBLE_EQ(t.ppq(), 1.0);
    synth::nudgeTransportCursor(t.transport, t.state, 1.0);
    t.settle();
    EXPECT_DOUBLE_EQ(t.ppq(), 2.0);
    synth::nudgeTransportCursor(t.transport, t.state, -1.0);
    t.settle();
    EXPECT_DOUBLE_EQ(t.ppq(), 1.0);
    synth::nudgeTransportCursor(t.transport, t.state, -1.0);
    t.settle();
    synth::nudgeTransportCursor(t.transport, t.state, -1.0);
    t.settle();
    EXPECT_DOUBLE_EQ(t.ppq(), 0.0) << "never negative";
}

TEST(TransportNudge, BarNudgeFollowsTheTimeSignature) {
    struct Case {
        int num, den;
        double beatsPerBar;
    };
    for (const auto c : {Case{4, 4, 4.0}, Case{3, 4, 3.0}, Case{6, 8, 3.0}, Case{7, 8, 3.5}}) {
        BareTransport t;
        ASSERT_TRUE(t.transport.setTimeSignature(c.num, c.den));
        t.settle();
        synth::nudgeTransportCursorBars(t.transport, t.state, 1.0);
        t.settle();
        EXPECT_DOUBLE_EQ(t.ppq(), c.beatsPerBar) << c.num << "/" << c.den;
        synth::nudgeTransportCursorBars(t.transport, t.state, -1.0);
        t.settle();
        EXPECT_DOUBLE_EQ(t.ppq(), 0.0) << c.num << "/" << c.den;
    }
}

TEST(TransportNudge, BarNudgeBackFromMidBarClampsAtZero) {
    BareTransport t;
    t.transport.locateBeat(2.5);
    t.settle();
    synth::nudgeTransportCursorBars(t.transport, t.state, -1.0);
    t.settle();
    EXPECT_DOUBLE_EQ(t.ppq(), 0.0);
}

TEST(TransportNudge, NudgesFiredInsideOneBlockAccumulateInsteadOfLosingSteps) {
    BareTransport t;
    for (int i = 0; i < 5; ++i)
        synth::nudgeTransportCursor(t.transport, t.state, 1.0);
    t.settle();
    EXPECT_DOUBLE_EQ(t.ppq(), 5.0) << "five nudges before the audio thread ticks must all count";

    // A different burst, mixing directions and a bar step, still lands where the arithmetic says.
    synth::nudgeTransportCursor(t.transport, t.state, -1.0);
    synth::nudgeTransportCursorBars(t.transport, t.state, 1.0);
    synth::nudgeTransportCursor(t.transport, t.state, 1.0);
    t.settle();
    EXPECT_DOUBLE_EQ(t.ppq(), 9.0);
}

TEST(TransportNudge, ANudgeAfterTheSnapshotMovedRecomputesFromTheSnapshot) {
    BareTransport t;
    synth::nudgeTransportCursor(t.transport, t.state, 1.0);
    t.settle();
    // Someone else relocates; the snapshot no longer matches what the pending request was based on.
    t.transport.locateBeat(10.0);
    t.settle();
    synth::nudgeTransportCursor(t.transport, t.state, 1.0);
    t.settle();
    EXPECT_DOUBLE_EQ(t.ppq(), 11.0);
}

TEST(TransportNudge, AStaleRequestExpiresEvenIfTheSnapshotHasNotChanged) {
    synth::TransportNudgeState state;
    synth::TransportService::PositionSnapshot snap;
    snap.ppq = 4.0;
    EXPECT_DOUBLE_EQ(synth::computeNudgeTarget(state, snap, 1.0, 1000), 5.0);
    EXPECT_DOUBLE_EQ(synth::computeNudgeTarget(state, snap, 1.0, 1000 + synth::kNudgeAccumulateWindowMs), 6.0);
    EXPECT_DOUBLE_EQ(synth::computeNudgeTarget(state, snap, 1.0, 1000 + 2 * synth::kNudgeAccumulateWindowMs + 1), 5.0)
        << "past the window the snapshot is authoritative again";
}

TEST(TransportNudge, JumpToLoopLocatorsGoToStartAndEnd) {
    BareTransport t;
    t.transport.setLoop(2.0, 6.0, true);
    t.settle();
    synth::jumpToLoopLocator(t.transport, t.state, false);
    t.settle();
    EXPECT_DOUBLE_EQ(t.ppq(), 2.0);
    synth::jumpToLoopLocator(t.transport, t.state, true);
    t.settle();
    EXPECT_DOUBLE_EQ(t.ppq(), 6.0);
}

TEST(TransportNudge, JumpToLoopIsANoOpWithoutALoopRange) {
    BareTransport t;
    t.transport.locateBeat(5.0);
    t.transport.setLoop(3.0, 3.0, false);
    t.settle();
    EXPECT_TRUE(synth::jumpToLoopLocator(t.transport, t.state, false));
    EXPECT_TRUE(synth::jumpToLoopLocator(t.transport, t.state, true));
    t.settle();
    EXPECT_DOUBLE_EQ(t.ppq(), 5.0);
}

TEST(TransportNudge, ANudgeAfterAJumpBuildsOnTheJumpTarget) {
    BareTransport t;
    t.transport.setLoop(8.0, 12.0, true);
    t.settle();
    synth::jumpToLoopLocator(t.transport, t.state, false);
    synth::nudgeTransportCursor(t.transport, t.state, 1.0);
    t.settle();
    EXPECT_DOUBLE_EQ(t.ppq(), 9.0);
}

// ============================================================================
// invokeDirectly dispatch through MainComponent
// ============================================================================

TEST(ShortcutManagerCursorActionsInvoke, BeatAndBarCommandsMoveTheCursor) {
    CursorApp app;
    ASSERT_TRUE(app.invoke(AppCommands::transportNudgeForwardBeat));
    app.settle();
    EXPECT_DOUBLE_EQ(app.ppq(), 1.0);
    ASSERT_TRUE(app.invoke(AppCommands::transportNudgeForwardBar));
    app.settle();
    EXPECT_DOUBLE_EQ(app.ppq(), 5.0);
    ASSERT_TRUE(app.invoke(AppCommands::transportNudgeBackBar));
    app.settle();
    EXPECT_DOUBLE_EQ(app.ppq(), 1.0);
    ASSERT_TRUE(app.invoke(AppCommands::transportNudgeBackBeat));
    app.settle();
    EXPECT_DOUBLE_EQ(app.ppq(), 0.0);
    ASSERT_TRUE(app.invoke(AppCommands::transportNudgeBackBeat));
    app.settle();
    EXPECT_DOUBLE_EQ(app.ppq(), 0.0) << "clamped at 0";
}

TEST(ShortcutManagerCursorActionsInvoke, BarCommandUsesTheCurrentTimeSignature) {
    CursorApp app;
    ASSERT_TRUE(app.engine.getTransport().setTimeSignature(3, 4));
    app.settle();
    ASSERT_TRUE(app.invoke(AppCommands::transportNudgeForwardBar));
    app.settle();
    EXPECT_DOUBLE_EQ(app.ppq(), 3.0);
}

TEST(ShortcutManagerCursorActionsInvoke, RapidCommandsWithinOneBlockAllCount) {
    CursorApp app;
    for (int i = 0; i < 3; ++i)
        ASSERT_TRUE(app.invoke(AppCommands::transportNudgeForwardBeat));
    app.settle();
    EXPECT_DOUBLE_EQ(app.ppq(), 3.0);
    ASSERT_TRUE(app.invoke(AppCommands::transportNudgeForwardBar));
    ASSERT_TRUE(app.invoke(AppCommands::transportNudgeBackBeat));
    app.settle();
    EXPECT_DOUBLE_EQ(app.ppq(), 6.0);
}

TEST(ShortcutManagerCursorActionsInvoke, NudgeWorksWhilePlayingWithoutStoppingTheTransport) {
    CursorApp app;
    auto& transport = app.engine.getTransport();
    ASSERT_TRUE(transport.play());
    app.settle();
    ASSERT_TRUE(transport.getPositionSnapshot().playing);
    const double before = app.ppq();
    ASSERT_TRUE(app.invoke(AppCommands::transportNudgeForwardBeat));
    app.settle();
    EXPECT_NEAR(app.ppq(), before + 1.0, 1e-9);
    EXPECT_TRUE(transport.getPositionSnapshot().playing);
}

TEST(ShortcutManagerCursorActionsInvoke, JumpToLoopCommandsLocateToTheLocators) {
    CursorApp app;
    auto& transport = app.engine.getTransport();
    ASSERT_TRUE(transport.setLoop(2.0, 6.0, true));
    app.settle();
    ASSERT_TRUE(app.invoke(AppCommands::transportJumpToLoopEnd));
    app.settle();
    EXPECT_DOUBLE_EQ(app.ppq(), 6.0);
    ASSERT_TRUE(app.invoke(AppCommands::transportJumpToLoopStart));
    app.settle();
    EXPECT_DOUBLE_EQ(app.ppq(), 2.0);
}

TEST(ShortcutManagerCursorActionsInvoke, JumpToLoopWithoutARangeDoesNothing) {
    CursorApp app;
    auto& transport = app.engine.getTransport();
    transport.locateBeat(7.0);
    ASSERT_TRUE(transport.setLoop(3.0, 3.0, false));
    app.settle();
    ASSERT_TRUE(app.invoke(AppCommands::transportJumpToLoopStart));
    ASSERT_TRUE(app.invoke(AppCommands::transportJumpToLoopEnd));
    app.settle();
    EXPECT_DOUBLE_EQ(app.ppq(), 7.0);
}
