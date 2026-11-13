// TimelineTopBarRingTests.cpp: a keyboard-only user reaches every control of the Timeline's top bar (tools, Sizing,
// snap, follow, transport, tempo, time signature, song key). Focus stays on the panel root; Up on it raises a ring, the
// arrows move it, Return and Space press the ringed control. Real focus needs a native peer, so the root's key is
// handed to handleRootFocusKey and the rest go through the panel's own keyPressed.
#include "../../TestWait.h"
#include "../Layout/BottomDockActiveTabResetGuard.h"
#include "../Mixer/MixerZonesTestRig.h"
#include "AppUndoManager.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Chrome/ToolbarButton/ToolbarButton.h"
#include "UI/Layout/KeyRing.h"
#include "UI/Timeline/ClipSizingButton.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <set>

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

// Regression test for FRO1524: Down from the bar left focus on the panel root, which draws no ring, and only a second
// Down reached Add Track.
TEST(TimelineTopBarRingTest, DownFromTheBarLandsOnAddTrackInOnePress) {
    TopBarFixture f;
    f.panel.setRecordFocusForTest(true);
    ASSERT_TRUE(f.panel.handleRootFocusKey(key(juce::KeyPress::upKey)));
    ASSERT_TRUE(f.keys().isActive());
    EXPECT_FALSE(f.panel.isAddTrackButtonFocused());

    EXPECT_TRUE(f.panel.keyPressed(key(juce::KeyPress::downKey)));
    EXPECT_FALSE(f.keys().isActive()) << "the bar's ring is off";
    EXPECT_TRUE(f.keys().getValueText().isEmpty());
    EXPECT_TRUE(f.panel.isAddTrackButtonFocused()) << "and the next stop shows the focus";

    // Escape keeps going back to the root, where no control is focused.
    f.panel.setRecordFocusForTest(true);
    ASSERT_TRUE(f.panel.handleRootFocusKey(key(juce::KeyPress::upKey)));
    EXPECT_TRUE(f.panel.keyPressed(key(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(f.keys().isActive());
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

// A menu on the ring opens with Return only; Space is left to the app's play/stop.
TEST(TimelineTopBarRingTest, ReturnOpensTheRingedKeyMenuButSpaceIsNotTheRings) {
    TopBarFixture f;
    ASSERT_TRUE(f.panel.handleRootFocusKey(key(juce::KeyPress::upKey)));
    auto& songKey = f.panel.getTransportBar().getSongKeyField();
    int opened = 0;
    songKey.onOpen = [&] { ++opened; };
    f.keys().handleKey(key(juce::KeyPress::homeKey));
    while (f.keys().getRingedStop() != &songKey && f.keys().getRingedStop() != f.keys().getStops().back())
        f.keys().handleKey(key(juce::KeyPress::rightKey));
    ASSERT_EQ(f.keys().getRingedStop(), &songKey);
    EXPECT_FALSE(f.keys().handleKey(key(juce::KeyPress::spaceKey))) << "the ring leaves Space to the app";
    EXPECT_EQ(opened, 0);
    EXPECT_TRUE(f.keys().handleKey(key(juce::KeyPress::returnKey)));
    EXPECT_EQ(opened, 1);
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

// ---- ToolbarBarsUseToolbarButtonTests ----
// ToolbarBarsUseToolbarButtonTests.cpp: the guard for the sibling-gap kind "a button on a toolbar that is not a
// ToolbarButton". The Timeline's transport row and the Mixer's toolbar are built of ToolbarButtons, which carry the
// bar's chip, icon, caption, "Show button labels" behaviour, hover tween and focus ring. A plain juce::TextButton in
// either bar misses all of that (the clip sizing control was one, with a text-only look and a width of its own), so
// the walk fails on any visible TextButton outside a short allow-list of controls that are deliberately not toolbar
// buttons.

namespace {

// Controls that sit in a bar but are not toolbar buttons, with the reason each stays a TextButton.
struct Allowed {
    const char* typeName; // part of typeid(...).name()
    const char* reason;
};
const std::vector<Allowed> kAllowed = {};

bool isAllowed(const juce::Component& c) {
    const juce::String type(typeid(c).name());
    for (const auto& allowed : kAllowed)
        if (type.contains(allowed.typeName))
            return true;
    return false;
}

juce::String describe(const juce::Component& c) {
    return juce::String(typeid(c).name()) + " \"" + c.getComponentID() + "\" " + c.getTitle() + " " +
           c.getBounds().toString();
}

// Every visible TextButton under `root` (at any depth) whose bounds sit inside `area` (root's own coordinates).
void collectTextButtons(juce::Component& root, juce::Rectangle<int> area, std::vector<juce::Component*>& found) {
    for (auto* child : root.getChildren()) {
        if (!child->isVisible())
            continue;
        if (dynamic_cast<juce::TextButton*>(child) != nullptr && area.contains(child->getBounds().getCentre()))
            found.push_back(child);
        collectTextButtons(*child, area.translated(-child->getX(), -child->getY()), found);
    }
}

} // namespace

TEST(ToolbarBarsUseToolbarButton, TheTimelineTransportRowHasNoPlainTextButton) {
    BottomDockActiveTabResetGuardMDT resetGuard;
    synth::TimelineDoc doc;
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1600, 400);
    panel.setTimelineDoc(&doc);

    std::vector<juce::Component*> found;
    collectTextButtons(panel, panel.getTransportBarBounds(), found);
    for (auto* button : found)
        EXPECT_TRUE(isAllowed(*button)) << "a plain TextButton in the Timeline transport row, use ToolbarButton: "
                                        << describe(*button);
    EXPECT_NE(dynamic_cast<synth::ui::ToolbarButton*>(&panel.getClipSizingButton()), nullptr);
    EXPECT_NE(dynamic_cast<synth::ui::ToolbarButton*>(&panel.getSnapToggleButton()), nullptr);
}

TEST(ToolbarBarsUseToolbarButton, TheMixerToolbarHasNoPlainTextButton) {
    MixerZonesRig r(1);
    auto& toolbar = r.panel->getToolbarForTest();
    std::vector<juce::Component*> found;
    collectTextButtons(toolbar, toolbar.getLocalBounds(), found);
    for (auto* button : found)
        EXPECT_TRUE(isAllowed(*button)) << "a plain TextButton in the Mixer toolbar, use ToolbarButton: "
                                        << describe(*button);
}
