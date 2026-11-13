// Concern: the transport bar's Return to Start button -- it relocates the transport to beat 0, it is named for
// screen readers and reachable from the keyboard, and it is not a MIDI Learn target.
#include "Transport/TransportService.h"
#include "UI/Timeline/TimelineTransportBar.h"
#include <gtest/gtest.h>

TEST(TimelineTransportBarReturnToStartTest, ClickLocatesTheTransportToBeatZero) {
    synth::TransportService transport;
    transport.prepare(48000.0, 512);
    synth::ui::TimelineTransportBar bar;
    bar.setTransport(&transport);
    bar.setSize(500, 28);

    transport.locateBeat(8.0);
    transport.tick(512);
    ASSERT_GT(transport.getPositionSnapshot().ppq, 0.0);

    bar.getReturnToStartButton().onClick();
    transport.tick(512);
    EXPECT_DOUBLE_EQ(transport.getPositionSnapshot().ppq, 0.0);
}

TEST(TimelineTransportBarReturnToStartTest, ClickRunsTheOwnersHookInsteadOfLocatingItself) {
    synth::TransportService transport;
    transport.prepare(48000.0, 512);
    synth::ui::TimelineTransportBar bar;
    bar.setTransport(&transport);
    bar.setSize(500, 28);

    int hookCalls = 0;
    bar.onReturnToStart = [&hookCalls] { ++hookCalls; };
    transport.locateBeat(8.0);
    transport.tick(512);
    bar.getReturnToStartButton().onClick();
    transport.tick(512);

    EXPECT_EQ(hookCalls, 1);
    EXPECT_GT(transport.getPositionSnapshot().ppq, 0.0) << "the hook owns the locate (the app's command tracks nudges)";
}

TEST(TimelineTransportBarReturnToStartTest, ClickWithNoTransportIsInert) {
    synth::ui::TimelineTransportBar bar;
    bar.setSize(500, 28);
    bar.getReturnToStartButton().onClick(); // must not crash
    SUCCEED();
}

TEST(TimelineTransportBarReturnToStartTest, IsNamedForScreenReadersAndReachableFromTheKeyboard) {
    synth::ui::TimelineTransportBar bar;
    bar.setSize(500, 28);
    auto& button = bar.getReturnToStartButton();

    EXPECT_EQ(button.getTitle(), "Return to Start");
    EXPECT_TRUE(button.getDescription().isNotEmpty());
    EXPECT_EQ(button.getTooltip(), "Return to Start");
    EXPECT_TRUE(button.getWantsKeyboardFocus());
    EXPECT_TRUE(button.isVisible());
    EXPECT_TRUE(button.isAccessible());
    EXPECT_FALSE(button.getBounds().isEmpty());
    EXPECT_EQ(button.getComponentID(), "timelineTransportReturnToStart");
}

TEST(TimelineTransportBarReturnToStartTest, EveryGlyphButtonHasAnAccessibleName) {
    synth::ui::TimelineTransportBar bar;
    bar.setSize(500, 28);
    for (auto* button : {&bar.getReturnToStartButton(), &bar.getPlayStopButton(), &bar.getRecordButton(),
                         &bar.getLoopButton(), &bar.getMetronomeButton()})
        EXPECT_TRUE(button->getTitle().isNotEmpty()) << button->getComponentID();
}

TEST(TimelineTransportBarReturnToStartTest, SitsBeforePlayStopWithoutOverlappingAnyButton) {
    synth::ui::TimelineTransportBar bar;
    bar.setSize(500, 34);
    const auto r = bar.getReturnToStartButton().getBounds();
    const auto p = bar.getPlayStopButton().getBounds();
    EXPECT_LT(r.getRight(), p.getX());
    EXPECT_FALSE(r.intersects(p));
    EXPECT_FALSE(r.intersects(bar.getRecordButton().getBounds()));
    EXPECT_FALSE(r.intersects(bar.getMetronomeButton().getBounds()));
}

TEST(TimelineTransportBarReturnToStartTest, IsNotAMidiLearnTarget) {
    synth::ui::TimelineTransportBar bar;
    bar.setSize(500, 34);
    EXPECT_TRUE(bar.findMidiLearnableActionForTest(&bar.getReturnToStartButton()).isEmpty());
    EXPECT_FALSE(bar.findMidiLearnableActionForTest(&bar.getPlayStopButton()).isEmpty());
    std::vector<synth::ui::PickCandidate> candidates;
    bar.collectPickCandidates(candidates);
    EXPECT_EQ(candidates.size(), 4u) << "the four MIDI-learnable glyph buttons only";
}
