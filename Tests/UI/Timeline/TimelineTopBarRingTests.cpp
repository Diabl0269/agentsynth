// TimelineTopBarRingTests.cpp: a keyboard-only user reaches every control of the Timeline's top bar (tools, Sizing,
// snap, follow, transport, tempo, time signature, song key). Focus stays on the panel root; Up on it raises a ring, the
// arrows move it, Return and Space press the ringed control. Real focus needs a native peer, so the root's key is
// handed to handleRootFocusKey and the rest go through the panel's own keyPressed.
#include "../../TestWait.h"
#include "../Layout/BottomDockActiveTabResetGuard.h"
#include "AppUndoManager.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Layout/KeyRing.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <algorithm>
#include <gtest/gtest.h>

namespace {

using synth::ui::EditTool;
using synth::ui::TimelinePanelComponent;

juce::KeyPress key(int code) { return juce::KeyPress(code, juce::ModifierKeys::noModifiers, 0); }

struct TopBarFixture {
    BottomDockActiveTabResetGuardMDT resetGuard;
    synth::TimelineDoc doc; // before the panel: the panel removes its listener in its destructor
    TimelinePanelComponent panel;

    TopBarFixture() {
        panel.setSize(1600, 400);
        panel.setTimelineDoc(&doc);
    }
    auto& keys() { return panel.getTopBarKeysForTest(); }
    std::vector<juce::Component*> everyControlOfTheBar() {
        auto& bar = panel.getTransportBar();
        std::vector<juce::Component*> all{&panel.getSidePaneButton(),
                                          &panel.getClipSizingButton(),
                                          &panel.getSnapToggleButton(),
                                          &panel.getSnapCombo(),
                                          &panel.getFollowPlayheadButtonForTest(),
                                          &bar.getReturnToStartButton(),
                                          &bar.getPlayStopButton(),
                                          &bar.getRecordButton(),
                                          &bar.getLoopButton(),
                                          &bar.getMetronomeButton(),
                                          &bar.getCountInCombo(),
                                          &bar.getBpmLabel(),
                                          &bar.getTimeSigLabel(),
                                          &bar.getSongKeyField()};
        for (auto tool : synth::ui::kAllEditTools)
            all.push_back(panel.getToolButton(tool));
        return all;
    }
};

} // namespace

// Regression test for FRO1114: tempo, time signature, the transport buttons and the tool strip had no keyboard path.
TEST(TimelineTopBarRingTest, UpFromTheRootThenRightReachesEveryControlLeftToRight) {
    TopBarFixture f;
    auto expected = f.everyControlOfTheBar();
    const auto stops = f.keys().getStops();
    for (auto* control : expected)
        EXPECT_NE(std::find(stops.begin(), stops.end(), control), stops.end())
            << "the bar skips " << control->getTitle() << " " << control->getName();
    EXPECT_EQ(stops.size(), expected.size());

    EXPECT_FALSE(f.keys().isActive());
    ASSERT_TRUE(f.panel.handleRootFocusKey(key(juce::KeyPress::upKey)));
    EXPECT_TRUE(f.keys().isActive());
    EXPECT_EQ(f.keys().getRingedStop(), stops.front());
    for (size_t i = 1; i < stops.size(); ++i) {
        ASSERT_TRUE(f.panel.keyPressed(key(juce::KeyPress::rightKey)));
        EXPECT_EQ(f.keys().getRingedStop(), stops[i]) << "stop " << i;
    }
    EXPECT_TRUE(f.panel.keyPressed(key(juce::KeyPress::rightKey))) << "no wrap: the last stop keeps the key";
    EXPECT_EQ(f.keys().getRingedStop(), stops.back());
    for (size_t i = stops.size() - 1; i-- > 0;) {
        ASSERT_TRUE(f.panel.keyPressed(key(juce::KeyPress::leftKey)));
        EXPECT_EQ(f.keys().getRingedStop(), stops[i]);
    }
    ASSERT_TRUE(f.panel.keyPressed(key(juce::KeyPress::endKey)));
    EXPECT_EQ(f.keys().getRingedStop(), stops.back());
    ASSERT_TRUE(f.panel.keyPressed(key(juce::KeyPress::homeKey)));
    EXPECT_EQ(f.keys().getRingedStop(), stops.front());
}

TEST(TimelineTopBarRingTest, TheStopsAreInOnScreenOrder) {
    TopBarFixture f;
    const auto stops = f.keys().getStops();
    int previousX = -1;
    for (auto* stop : stops) {
        const int x = f.panel.getLocalArea(stop->getParentComponent(), stop->getBounds()).getX();
        EXPECT_GT(x, previousX) << stop->getName();
        previousX = x;
    }
}

TEST(TimelineTopBarRingTest, DownAndEscapeLeaveTheBarAndOtherKeysStillReachThePanel) {
    TopBarFixture f;
    ASSERT_TRUE(f.panel.handleRootFocusKey(key(juce::KeyPress::upKey)));
    EXPECT_TRUE(f.panel.keyPressed(key(juce::KeyPress::downKey)));
    EXPECT_FALSE(f.keys().isActive());
    ASSERT_TRUE(f.panel.handleRootFocusKey(key(juce::KeyPress::upKey)));
    EXPECT_TRUE(f.panel.keyPressed(key(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(f.keys().isActive());

    ASSERT_TRUE(f.panel.handleRootFocusKey(key(juce::KeyPress::upKey)));
    EXPECT_FALSE(f.keys().handleKey(juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys::commandModifier, 0)))
        << "a chord is the panel's, not the bar's";
    f.keys().leave();
    EXPECT_FALSE(f.keys().handleKey(key(juce::KeyPress::rightKey))) << "nothing is ringed once it has left";
}

TEST(TimelineTopBarRingTest, ReturnAndSpacePressTheRingedControl) {
    TopBarFixture f;
    ASSERT_TRUE(f.panel.handleRootFocusKey(key(juce::KeyPress::upKey)));
    const auto stops = f.keys().getStops();
    const auto ringTo = [&](juce::Component* target) {
        f.keys().handleKey(key(juce::KeyPress::homeKey));
        while (f.keys().getRingedStop() != target && f.keys().getRingedStop() != stops.back())
            f.keys().handleKey(key(juce::KeyPress::rightKey));
        ASSERT_EQ(f.keys().getRingedStop(), target);
    };

    const bool snapBefore = f.panel.getSnapToggleButton().getToggleState();
    ringTo(&f.panel.getSnapToggleButton());
    EXPECT_TRUE(f.panel.keyPressed(key(juce::KeyPress::returnKey)));
    synth::test::flushMessageQueue(); // a button's click is delivered as a message
    EXPECT_NE(f.panel.getSnapToggleButton().getToggleState(), snapBefore);

    ringTo(f.panel.getToolButton(EditTool::Split));
    EXPECT_TRUE(f.panel.keyPressed(key(juce::KeyPress::spaceKey)));
    synth::test::waitUntil([&] { return (f.panel.getActiveTool()) == (EditTool::Split); });
    EXPECT_EQ(f.panel.getActiveTool(), EditTool::Split);

    ringTo(&f.panel.getTransportBar().getBpmLabel());
    EXPECT_TRUE(f.panel.keyPressed(key(juce::KeyPress::returnKey)));
    EXPECT_TRUE(f.panel.getTransportBar().getBpmLabel().isBeingEdited()) << "Return starts typing the tempo";
}

// Regression test for FRO1291: the ring moved a painted box while the panel kept focus, so a screen reader heard
// nothing. The panel's handler value is the ringed control's name, and it changes on every move.
TEST(TimelineTopBarRingTest, TheScreenReaderValueNamesTheRingedControlAndChangesWithEveryMove) {
    TopBarFixture f;
    auto handler = f.keys().createAccessibilityHandler();
    ASSERT_NE(handler, nullptr);
    EXPECT_EQ(handler->getRole(), juce::AccessibilityRole::group);
    ASSERT_NE(handler->getValueInterface(), nullptr);
    const auto value = [&] { return handler->getValueInterface()->getCurrentValueAsString(); };
    const auto nameOf = [](juce::Component* c) {
        auto* b = dynamic_cast<juce::Button*>(c);
        return c->getTitle().isNotEmpty() ? c->getTitle() : b != nullptr ? b->getButtonText() : c->getName();
    };

    EXPECT_TRUE(value().isEmpty()) << "no ring, no value";
    ASSERT_TRUE(f.panel.handleRootFocusKey(key(juce::KeyPress::upKey)));
    const auto stops = f.keys().getStops();
    EXPECT_EQ(value(), nameOf(stops[0]));
    EXPECT_TRUE(value().isNotEmpty());
    ASSERT_TRUE(f.panel.keyPressed(key(juce::KeyPress::rightKey)));
    EXPECT_EQ(value(), nameOf(stops[1]));
    ASSERT_TRUE(f.panel.keyPressed(key(juce::KeyPress::endKey)));
    EXPECT_EQ(value(), nameOf(stops.back()));
    EXPECT_NE(value(), nameOf(stops[0]));
    ASSERT_TRUE(f.panel.keyPressed(key(juce::KeyPress::downKey)));
    EXPECT_TRUE(value().isEmpty()) << "back on the panel";
}
