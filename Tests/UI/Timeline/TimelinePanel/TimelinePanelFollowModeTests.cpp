// TimelinePanelFollowModeTests.cpp
//
// Follow playhead's modes: the pure view-origin rule, then the panel (Ctrl+F through the real keyPressed, persistence,
// the Smooth view move on the transport poll, the follow button's right-click menu and its tooltip).

#include "ShortcutManager/ShortcutManager.h"
#include "UI/Timeline/FollowPlayhead.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include <cmath>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using synth::ui::FollowMode;

namespace {
struct IsolatedProps {
    juce::PropertiesFile::Options opts;
    juce::ApplicationProperties props;

    explicit IsolatedProps(const char* name) {
        opts.applicationName = name;
        opts.folderName = name;
        opts.filenameSuffix = "settings";
        opts.osxLibrarySubFolder = "Application Support";
        opts.storageFormat = juce::PropertiesFile::storeAsXML;
        {
            juce::ApplicationProperties initial;
            initial.setStorageParameters(opts);
            if (auto* s = initial.getUserSettings())
                s->getFile().deleteFile();
        }
        props.setStorageParameters(opts);
    }
    ~IsolatedProps() {
        if (auto* s = props.getUserSettings())
            s->getFile().deleteFile();
    }
};

synth::TransportService::PositionSnapshot playingAt(double ppq) {
    synth::TransportService::PositionSnapshot snap;
    snap.playing = true;
    snap.ppq = ppq;
    return snap;
}

juce::KeyPress cycleKeyFor(const ShortcutManager* manager) {
    return manager != nullptr
               ? manager->getBinding("timelineFollowModeCycle")
               : juce::KeyPress(
                     'f',
                     ShortcutManager::hostDefaultsPlatform() == ShortcutManager::DefaultsPlatform::Mac
                         ? juce::ModifierKeys(juce::ModifierKeys::ctrlModifier)
                         : juce::ModifierKeys(juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier),
                     0);
}
} // namespace

// ---- The pure rule ---------------------------------------------------------------------------------------------

TEST(FollowViewOriginTest, PageKeepsTheOriginWhileTheCursorIsInsideAndLandsATenthInWhenItLeaves) {
    using synth::ui::followViewOrigin;
    EXPECT_DOUBLE_EQ(followViewOrigin(FollowMode::Page, 50, 6.0, 4.0, 10.0), 4.0) << "inside: no move";
    EXPECT_DOUBLE_EQ(followViewOrigin(FollowMode::Page, 50, 14.0, 4.0, 10.0), 4.0) << "on the edge counts as inside";
    EXPECT_DOUBLE_EQ(followViewOrigin(FollowMode::Page, 50, 30.0, 4.0, 10.0), 29.0) << "a tenth of the width in";
    EXPECT_DOUBLE_EQ(followViewOrigin(FollowMode::Page, 50, 1.0, 4.0, 10.0), 0.0) << "never negative";
}

TEST(FollowViewOriginTest, SmoothPinsTheCursorAtThePointAndStopsAtTheStartOfTheSong) {
    using synth::ui::followViewOrigin;
    EXPECT_DOUBLE_EQ(followViewOrigin(FollowMode::Smooth, 50, 30.0, 0.0, 10.0), 25.0);
    EXPECT_DOUBLE_EQ(followViewOrigin(FollowMode::Smooth, 25, 30.0, 0.0, 10.0), 27.5);
    EXPECT_DOUBLE_EQ(followViewOrigin(FollowMode::Smooth, 50, 3.0, 9.0, 10.0), 0.0)
        << "near the start the cursor travels to the pin first";
    EXPECT_DOUBLE_EQ(followViewOrigin(FollowMode::Smooth, 50, 30.0, 25.0, 10.0), 25.0) << "stable once pinned";
}

TEST(FollowViewOriginTest, ThePointIsClampedToTenToNinetyPercentAndBadInputKeepsTheView) {
    using synth::ui::followViewOrigin;
    EXPECT_DOUBLE_EQ(followViewOrigin(FollowMode::Smooth, 0, 30.0, 0.0, 10.0), 29.0);
    EXPECT_DOUBLE_EQ(followViewOrigin(FollowMode::Smooth, 100, 30.0, 0.0, 10.0), 21.0);
    EXPECT_DOUBLE_EQ(followViewOrigin(FollowMode::Smooth, 50, std::nan(""), 7.0, 10.0), 7.0);
    EXPECT_DOUBLE_EQ(followViewOrigin(FollowMode::Smooth, 50, 30.0, 7.0, 0.0), 7.0);
}

TEST(FollowViewOriginTest, TheModeKeyRoundTripsAndAMissingOrUnknownKeyIsPage) {
    EXPECT_EQ(synth::ui::followModeFromKey(synth::ui::followModeKey(FollowMode::Smooth)), FollowMode::Smooth);
    EXPECT_EQ(synth::ui::followModeFromKey(synth::ui::followModeKey(FollowMode::Page)), FollowMode::Page);
    EXPECT_EQ(synth::ui::followModeFromKey(""), FollowMode::Page);
    EXPECT_EQ(synth::ui::followModeFromKey("bogus"), FollowMode::Page);
}

// ---- The panel ------------------------------------------------------------------------------------------------

TEST(TimelineFollowModeTest, DefaultsToPageAtFiftyPercent) {
    synth::ui::TimelinePanelComponent panel;
    EXPECT_EQ(panel.getFollowMode(), FollowMode::Page);
    EXPECT_EQ(panel.getFollowPointPercent(), 50);
}

// Ctrl+F (Ctrl+Alt+F off the Mac) arrives as a real KeyPress through keyPressed, flips the mode, persists it, and
// leaves the follow on/off flag alone.
TEST(TimelineFollowModeTest, CtrlFFlipsTheModePersistsItAndLeavesFollowAlone) {
    IsolatedProps guard("Agent Synth Timeline Follow Mode Test");
    ShortcutManager shortcuts;
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 320);
    panel.setApplicationProperties(&guard.props);
    panel.setShortcutManager(&shortcuts);
    ASSERT_EQ(shortcuts.getBinding("timelineFollowModeCycle"), cycleKeyFor(&shortcuts));
    ASSERT_FALSE(panel.isFollowPlayheadEnabled());

    EXPECT_TRUE(panel.keyPressed(cycleKeyFor(&shortcuts)));
    EXPECT_EQ(panel.getFollowMode(), FollowMode::Smooth);
    EXPECT_FALSE(panel.isFollowPlayheadEnabled()) << "the mode key does not toggle follow";
    EXPECT_EQ(guard.props.getUserSettings()->getValue("timelineFollowMode"), "smooth");

    synth::ui::TimelinePanelComponent restored;
    restored.setSize(1200, 320);
    restored.setApplicationProperties(&guard.props);
    EXPECT_EQ(restored.getFollowMode(), FollowMode::Smooth) << "restored on the next start";

    EXPECT_TRUE(panel.keyPressed(cycleKeyFor(&shortcuts)));
    EXPECT_EQ(panel.getFollowMode(), FollowMode::Page);
    EXPECT_EQ(guard.props.getUserSettings()->getValue("timelineFollowMode"), "page");

    // F is still the on/off toggle, and a bare F is not the mode key.
    EXPECT_TRUE(panel.keyPressed(juce::KeyPress('f')));
    EXPECT_TRUE(panel.isFollowPlayheadEnabled());
    EXPECT_EQ(panel.getFollowMode(), FollowMode::Page);
    panel.setShortcutManager(nullptr);
}

TEST(TimelineFollowModeTest, TheModeKeyWorksWithNoShortcutManagerToo) {
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 320);
    EXPECT_TRUE(panel.keyPressed(cycleKeyFor(nullptr)));
    EXPECT_EQ(panel.getFollowMode(), FollowMode::Smooth);
}

TEST(TimelineFollowModeTest, AMissingSettingsKeyMigratesToPageAtFiftyAndThePointIsClamped) {
    IsolatedProps guard("Agent Synth Timeline Follow Migrate Test");
    guard.props.getUserSettings()->setValue("timelineFollowPlayhead", true); // an install from before modes
    synth::ui::TimelinePanelComponent panel;
    panel.setApplicationProperties(&guard.props);
    EXPECT_TRUE(panel.isFollowPlayheadEnabled());
    EXPECT_EQ(panel.getFollowMode(), FollowMode::Page);
    EXPECT_EQ(panel.getFollowPointPercent(), 50);

    panel.setFollowPointPercent(5);
    EXPECT_EQ(panel.getFollowPointPercent(), 10);
    panel.setFollowPointPercent(200);
    EXPECT_EQ(panel.getFollowPointPercent(), 90);
    EXPECT_EQ(guard.props.getUserSettings()->getIntValue("timelineFollowPointPercent"), 90);
}

namespace {
struct SmoothFixture {
    synth::ui::TimelinePanelComponent panel;
    SmoothFixture() {
        panel.setSize(1200, 320);
        panel.getViewState().pixelsPerBeat = 40.0;
        panel.getViewState().firstVisibleBeat = 0.0;
        panel.setFollowPlayheadEnabled(true);
        panel.setFollowMode(FollowMode::Smooth);
    }
    double visibleBeats() { return (double)panel.getClipLaneArea().getWidth() / panel.getViewState().pixelsPerBeat; }
};
} // namespace

TEST(TimelineFollowModeTest, SmoothScrollsTheViewSoThePlayheadSitsAtThePoint) {
    SmoothFixture f;
    const double visible = f.visibleBeats();
    const double beat = visible * 3.0;

    f.panel.updateFromTransport(playingAt(beat), 0.0);
    EXPECT_NEAR(f.panel.getViewState().firstVisibleBeat, beat - 0.5 * visible, 1e-9);

    f.panel.setFollowPointPercent(25); // applied live
    f.panel.updateFromTransport(playingAt(beat), 0.0);
    EXPECT_NEAR(f.panel.getViewState().firstVisibleBeat, beat - 0.25 * visible, 1e-9);

    // Even a playhead that is still on screen moves the view: that is the difference from Page.
    f.panel.getViewState().firstVisibleBeat = 0.0;
    f.panel.updateFromTransport(playingAt(visible * 0.2), 0.0);
    EXPECT_DOUBLE_EQ(f.panel.getViewState().firstVisibleBeat, 0.0) << "left of the pin at the start: origin stays 0";
}

TEST(TimelineFollowModeTest, SmoothDoesNothingStoppedOrWithFollowOff) {
    SmoothFixture f;
    const double beat = f.visibleBeats() * 3.0;
    auto stopped = playingAt(beat);
    stopped.playing = false;
    f.panel.updateFromTransport(stopped, 0.0);
    EXPECT_DOUBLE_EQ(f.panel.getViewState().firstVisibleBeat, 0.0);

    f.panel.setFollowPlayheadEnabled(false);
    f.panel.updateFromTransport(playingAt(beat), 0.0);
    EXPECT_DOUBLE_EQ(f.panel.getViewState().firstVisibleBeat, 0.0);
}

TEST(TimelineFollowModeTest, PageStillJumpsAPageAfterSwitchingBack) {
    SmoothFixture f;
    f.panel.setFollowMode(FollowMode::Page);
    const double visible = f.visibleBeats();
    f.panel.updateFromTransport(playingAt(visible + 1.0), 0.0);
    EXPECT_DOUBLE_EQ(f.panel.getViewState().firstVisibleBeat, visible + 1.0 - 0.1 * visible);
}

TEST(TimelineFollowModeTest, ASidewaysScrollStopsFollowingInBothModes) {
    SmoothFixture f;
    juce::MouseWheelDetails wheel{};
    wheel.deltaX = 0.5f;
    const auto event = juce::MouseEvent(
        juce::Desktop::getInstance().getMainMouseSource(), {10.0f, 10.0f}, juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, &f.panel, &f.panel, juce::Time::getCurrentTime(), {10.0f, 10.0f}, juce::Time::getCurrentTime(), 1, false);
    f.panel.mouseWheelMove(event, wheel);
    EXPECT_FALSE(f.panel.isFollowPlayheadEnabled()) << "the user took the view: follow lets go, as in Page";
}

TEST(TimelineFollowModeTest, RightClickingTheFollowButtonOpensThePageSmoothMenuWithoutToggling) {
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 320);
    auto& button = panel.getFollowPlayheadButtonForTest();
    juce::PopupMenu captured;
    int shown = 0;
    button.showMenuHook = [&](juce::PopupMenu& menu) {
        captured = menu;
        ++shown;
    };

    const juce::Point<float> centre = button.getLocalBounds().getCentre().toFloat();
    const auto rightClick = juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), centre,
                                             juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier), 0.0f, 0.0f,
                                             0.0f, 0.0f, 0.0f, &button, &button, juce::Time::getCurrentTime(), centre,
                                             juce::Time::getCurrentTime(), 1, false);
    static_cast<juce::Component&>(button).mouseDown(rightClick);
    static_cast<juce::Component&>(button).mouseUp(rightClick);

    ASSERT_EQ(shown, 1);
    EXPECT_FALSE(panel.isFollowPlayheadEnabled()) << "a right-click opens the menu, it does not toggle follow";

    std::vector<std::pair<juce::String, bool>> items; // text, ticked
    for (juce::PopupMenu::MenuItemIterator it(captured); it.next();)
        items.emplace_back(it.getItem().text, it.getItem().isTicked);
    ASSERT_EQ(items.size(), 2u);
    EXPECT_TRUE(items[0].first.startsWith("Page"));
    EXPECT_TRUE(items[1].first.startsWith("Smooth"));
    EXPECT_TRUE(items[0].second) << "the current mode is ticked";
    EXPECT_FALSE(items[1].second);

    // Choosing an item sets the mode; the menu is then ticked the other way.
    button.onModeChosen(FollowMode::Smooth);
    EXPECT_EQ(panel.getFollowMode(), FollowMode::Smooth);
    int ticked = 0;
    const auto menuAfter = button.buildMenu();
    for (juce::PopupMenu::MenuItemIterator it(menuAfter); it.next();)
        if (it.getItem().isTicked)
            ticked = it.getItem().itemID;
    EXPECT_EQ(ticked, synth::ui::FollowPlayheadButton::kSmoothItemId);

    // The keyboard route: Shift+F10 asks the focused provider for the same menu.
    EXPECT_TRUE(button.showContextMenuForKeyboardFocus());
    EXPECT_EQ(shown, 2);
}

TEST(TimelineFollowModeTest, ALeftClickStillTogglesFollow) {
    synth::ui::TimelinePanelComponent panel;
    panel.setSize(1200, 320);
    panel.getFollowPlayheadButtonForTest().onClick();
    EXPECT_TRUE(panel.isFollowPlayheadEnabled());
}

TEST(TimelineFollowModeTest, TheTooltipNamesTheModeAndTheKeysAndFollowsTheMode) {
    ShortcutManager shortcuts;
    synth::ui::TimelinePanelComponent panel;
    panel.setShortcutManager(&shortcuts);
    auto& button = panel.getFollowPlayheadButtonForTest();
    const auto cycle = shortcutHintFor(&shortcuts, "timelineFollowModeCycle", {});
    ASSERT_FALSE(cycle.isEmpty());

    EXPECT_TRUE(button.getTooltip().contains("Page mode")) << button.getTooltip();
    EXPECT_TRUE(button.getTooltip().contains(cycle + " switches mode")) << button.getTooltip();
    EXPECT_TRUE(button.getTooltip().endsWith("(f)")) << "the on/off key still closes the tooltip";

    panel.setFollowMode(FollowMode::Smooth);
    EXPECT_TRUE(button.getTooltip().contains("Smooth mode")) << button.getTooltip();

    shortcuts.setBinding("timelineFollowModeCycle", juce::KeyPress());
    shortcuts.saveToProperties();
    EXPECT_FALSE(button.getTooltip().contains("switches mode")) << "an unbound mode key is not advertised";
    panel.setShortcutManager(nullptr);
}

TEST(TimelineFollowModeTest, TheModeKeyIsUniqueInTheTimelineCategoryOnBothPlatforms) {
    for (const auto platform : {ShortcutManager::DefaultsPlatform::Mac, ShortcutManager::DefaultsPlatform::Other}) {
        ShortcutManager manager;
        manager.setDefaultsPlatform(platform);
        manager.resetToDefaults();
        const auto key = manager.getBinding("timelineFollowModeCycle");
        ASSERT_TRUE(key.isValid());
        EXPECT_NE(key, manager.getBinding("focusLibrarySearch")) << "Ctrl+F must not collapse into Cmd+F";
        EXPECT_NE(key, manager.getBinding("timelineFollowPlayheadToggle"));
        int sameInTimeline = 0;
        for (const auto& id : manager.getActionIds())
            if (ShortcutManager::getCategory(id) == ShortcutCategory::Timeline && manager.getBinding(id) == key)
                ++sameInTimeline;
        EXPECT_EQ(sameInTimeline, 1) << "platform " << (int)platform;
    }
}
