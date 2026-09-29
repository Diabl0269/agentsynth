// MixerSectionPanelTests.cpp (docs/mixer/panel.md#shared-sections): the shared Inserts/Sends/EQ
// sections across a real docked mixer -- faders line up whatever each column holds, the rail's
// chevron and a hidden section's strip show/hide a section in every column (the EQ one included), a
// divider drag resizes every column in whole rows and grows the bottom dock once the fader is at its
// minimum, a long list scrolls inside its section, and the sections persist app-wide. Real off-screen
// MainComponent, synthesized mouse events into the real handlers.

#include "../Layout/BottomDockActiveTabResetGuard.h"
#include "../Timeline/TimelinePanel/TimelinePanelTestFixture.h"
#include "UI/Layout/BottomDockComponent.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UserSettings.h"
#include <gtest/gtest.h>

namespace {

using synth::ui::MixerSection;
using Layout = synth::ui::MixerSectionLayout;
using Dock = synth::ui::BottomDockComponent;

const juce::StringArray& sectionKeys() {
    static const juce::StringArray keys{Layout::kInsertsHeightKey, Layout::kSendsHeightKey, Layout::kInsertsHiddenKey,
                                        Layout::kSendsHiddenKey, Layout::kEqHiddenKey};
    return keys;
}

juce::MouseEvent eventAt(juce::Component& target, juce::Point<float> pos, juce::Point<float> downPos, int clicks = 1,
                         bool dragged = false) {
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos,
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &target, &target, now, downPos, now, clicks, dragged);
}

void click(juce::Component& component) {
    const auto pos = component.getLocalBounds().getCentre().toFloat();
    component.mouseDown(eventAt(component, pos, pos));
    component.mouseUp(eventAt(component, pos, pos));
}

// A pointer at a fixed SCREEN y, expressed in `target`'s current local frame -- `target` may have
// moved since the press (a drag that grows the dock moves the whole panel up).
juce::Point<float> localAtScreenY(juce::Component& target, juce::Point<int> downScreen, int screenY) {
    return target.getLocalPoint(nullptr, juce::Point<int>(downScreen.x, screenY)).toFloat();
}

// Presses on `divider`'s centre, drags the pointer `dy` screen pixels, and optionally releases.
struct DividerDrag {
    juce::Component& divider;
    juce::Point<int> downScreen;
    juce::Point<float> downLocal;

    explicit DividerDrag(juce::Component& d)
        : divider(d)
        , downScreen(d.localPointToGlobal(d.getLocalBounds().getCentre()))
        , downLocal(d.getLocalBounds().getCentre().toFloat()) {
        divider.mouseDown(eventAt(divider, downLocal, downLocal));
    }
    void moveBy(int dy) {
        const auto pos = localAtScreenY(divider, downScreen, downScreen.y + dy);
        divider.mouseDrag(eventAt(divider, pos, downLocal, 1, true));
    }
    void release(int dy) {
        const auto pos = localAtScreenY(divider, downScreen, downScreen.y + dy);
        divider.mouseUp(eventAt(divider, pos, downLocal, 1, true));
    }
};

int yInPanel(synth::ui::MixerPanelComponent& panel, juce::Component& c) {
    return panel.getLocalArea(&c, c.getLocalBounds()).getY();
}

class MixerSectionPanelTest : public TimelinePanelIntegrationTest {
protected:
    void SetUp() override {
        TimelinePanelIntegrationTest::SetUp();
        resetSectionKeys();
    }
    void TearDown() override {
        mc.reset();
        resetSectionKeys();
        TimelinePanelIntegrationTest::TearDown();
    }

    static void resetSectionKeys() {
        juce::ApplicationProperties props;
        props.setStorageParameters(synth::userSettingsOptions());
        if (auto* s = props.getUserSettings()) {
            for (const auto& key : sectionKeys())
                s->removeValue(key);
            s->saveIfNeeded();
        }
    }

    // An open bottom dock on the Mixer tab, `tracks` audio tracks (each its own strip column).
    void openMixer(int tracks = 2) {
        mc = std::make_unique<MainComponent>(std::make_unique<MockProviderTL>());
        mc->setSize(1600, 900);
        mc->getAudioEngine().suspendDeviceCallback();
        mc->newPatchForTest();
        for (int i = 0; i < tracks; ++i)
            mc->simulateAddAudioTrackClick();
        mc->simulateToggleBottomPanelClick();
        mc->getBottomDock().setActiveTab(Dock::Tab::Mixer);
        panel().rebuild();
    }

    // Grows the dock through its own resize path, so a test starts with room to spare.
    void setDockHeight(int height) {
        mc->getBottomDock().onResizeHeight(height);
        panel().resized();
    }

    void addInserts(int columnIndex, int count) {
        for (int i = 0; i < count; ++i) {
            auto* column = panel().getStripColumnForTest(columnIndex);
            ASSERT_NE(column, nullptr);
            column->getInsertListForTest().addModule("Compressor");
            panel().rebuild();
        }
    }

    synth::ui::MixerPanelComponent& panel() { return mc->getBottomDock().getMixerPanel(); }
    Layout& layout() { return panel().getSectionLayout(); }

    std::vector<synth::ui::MixerColumnComponent*> strips() {
        std::vector<synth::ui::MixerColumnComponent*> out;
        for (int i = 0; auto* c = panel().getStripColumnForTest(i); ++i)
            out.push_back(c);
        return out;
    }

    static int persistedInt(MainComponent& m, const char* key) {
        return m.getAppPropertiesForTest().getUserSettings()->getIntValue(key, -1);
    }

    std::unique_ptr<MainComponent> mc;
    BottomDockActiveTabResetGuardMDT tabGuard_;
};

} // namespace

TEST_F(MixerSectionPanelTest, FadersShareOneTopAcrossColumnsWithDifferentInsertCounts) {
    openMixer(2);
    setDockHeight(600);
    const int baseInserts = strips()[0]->getInsertListForTest().getEntryCountForTest();
    addInserts(0, 3);
    auto s = strips();
    ASSERT_EQ(s.size(), 2u);
    ASSERT_EQ(s[0]->getInsertListForTest().getEntryCountForTest(), baseInserts + 3);
    ASSERT_EQ(s[1]->getInsertListForTest().getEntryCountForTest(), baseInserts);
    auto* master = panel().getMasterColumnForTest();
    ASSERT_NE(master, nullptr);
    ASSERT_TRUE(master->isVisible());

    const int faderTop = yInPanel(panel(), s[0]->getFaderForTest());
    EXPECT_EQ(yInPanel(panel(), s[1]->getFaderForTest()), faderTop) << "3 more inserts, the same fader line";
    EXPECT_EQ(yInPanel(panel(), s[0]->getSectionViewportForTest(MixerSection::Sends)),
              yInPanel(panel(), s[1]->getSectionViewportForTest(MixerSection::Sends)));
    EXPECT_EQ(yInPanel(panel(), master->getMeterReadoutForTest()), yInPanel(panel(), s[0]->getMeterReadoutForTest()))
        << "Master has no source line, pan or sends, yet its fader row lines up with every strip's";
    EXPECT_EQ(yInPanel(panel(), master->getInsertViewportForTest()),
              yInPanel(panel(), s[0]->getSectionViewportForTest(MixerSection::Inserts)));
}

TEST_F(MixerSectionPanelTest, RailChevronHidesASectionInEveryColumnAndShowsItAgain) {
    openMixer(2);
    setDockHeight(600);
    auto& toggle = panel().getSectionRailForTest().getToggleButtonForTest(MixerSection::Sends);
    ASSERT_TRUE(toggle.isVisible());
    ASSERT_GT(toggle.getHeight(), 0);
    EXPECT_EQ(toggle.getTitle(), "Hide Sends");
    const int faderBefore = strips()[0]->getFaderForTest().getHeight();

    click(toggle);
    EXPECT_TRUE(layout().isHidden(MixerSection::Sends));
    for (auto* column : strips()) {
        EXPECT_FALSE(column->getSectionViewportForTest(MixerSection::Sends).isVisible());
        auto& strip = column->getCollapsedSectionForTest(MixerSection::Sends);
        EXPECT_TRUE(strip.isVisible());
        EXPECT_EQ(strip.getHeight(), Layout::kCollapsedHeight);
        EXPECT_EQ(strip.getSummary(), "no sends");
        EXPECT_EQ(column->getFaderForTest().getHeight(), faderBefore + 60 - Layout::kCollapsedHeight)
            << "the fader gets the space back";
    }
    EXPECT_EQ(panel().getSectionRailForTest().getToggleButtonForTest(MixerSection::Sends).getTitle(), "Show Sends");
    EXPECT_EQ(persistedInt(*mc, Layout::kSendsHiddenKey), 1) << "persisted on the click";

    click(panel().getSectionRailForTest().getToggleButtonForTest(MixerSection::Sends));
    EXPECT_FALSE(layout().isHidden(MixerSection::Sends));
    for (auto* column : strips())
        EXPECT_TRUE(column->getSectionViewportForTest(MixerSection::Sends).isVisible());
}

// The EQ curve's own show/hide is the same control: the rail chevron hides it in every column, and
// clicking the 14 px strip a column leaves behind brings it back.
TEST_F(MixerSectionPanelTest, EqToggleHidesTheCurveEverywhereAndTheStripBringsItBack) {
    openMixer(2);
    setDockHeight(600);
    auto* column = strips()[0];
    column->getInsertListForTest().addModule("Parametric EQ");
    panel().rebuild();
    column = strips()[0];
    ASSERT_TRUE(column->getEqThumbnailForTest().isVisible());
    ASSERT_EQ(column->getEqThumbnailForTest().getHeight(), Layout::kEqHeight);

    click(panel().getSectionRailForTest().getToggleButtonForTest(MixerSection::Eq));
    ASSERT_TRUE(layout().isHidden(MixerSection::Eq));
    EXPECT_EQ(column->getEqThumbnailForTest().getHeight(), 0) << "the curve is gone";
    EXPECT_TRUE(column->getEqThumbnailForTest().isVisible()) << "isVisible() still means this column has an EQ";
    EXPECT_EQ(column->getCollapsedSectionForTest(MixerSection::Eq).getSummary(), "EQ");
    auto* other = strips()[1];
    EXPECT_EQ(other->getCollapsedSectionForTest(MixerSection::Eq).getSummary(),
              other->getEqThumbnailForTest().isVisible() ? "EQ" : "no EQ");
    EXPECT_EQ(persistedInt(*mc, Layout::kEqHiddenKey), 1);

    auto& otherStrip = strips()[1]->getCollapsedSectionForTest(MixerSection::Eq);
    ASSERT_TRUE(otherStrip.isVisible());
    click(otherStrip);
    EXPECT_FALSE(layout().isHidden(MixerSection::Eq)) << "a strip click in ANY column shows it again";
    EXPECT_EQ(column->getEqThumbnailForTest().getHeight(), Layout::kEqHeight);
}

TEST_F(MixerSectionPanelTest, DividerDragResizesEveryColumnInWholeRowsAndPersistsOnRelease) {
    openMixer(2);
    setDockHeight(700);
    auto s = strips();
    auto& rail = panel().getSectionRailForTest();

    DividerDrag drag(s[1]->getSectionDividerForTest(MixerSection::Inserts));
    EXPECT_EQ(layout().getHoveredDivider(), -1);
    drag.moveBy(40); // 90 + 40 = 130 -> 7 rows of 18
    EXPECT_EQ(rail.getDragBubbleTextForTest(), "7 rows");
    for (auto* column : strips())
        EXPECT_EQ(column->getSectionViewportForTest(MixerSection::Inserts).getHeight(), 126);
    EXPECT_EQ(panel().getMasterColumnForTest()->getInsertViewportForTest().getHeight(), 126);
    EXPECT_EQ(persistedInt(*mc, Layout::kInsertsHeightKey), -1) << "nothing persists mid-drag";

    drag.release(40);
    EXPECT_EQ(rail.getDragBubbleTextForTest(), "");
    EXPECT_EQ(persistedInt(*mc, Layout::kInsertsHeightKey), 126);

    // Double-click resets to the default.
    auto& divider = s[0]->getSectionDividerForTest(MixerSection::Inserts);
    const auto centre = divider.getLocalBounds().getCentre().toFloat();
    divider.mouseDoubleClick(eventAt(divider, centre, centre, 2));
    EXPECT_EQ(layout().getRequestedHeight(MixerSection::Inserts), 90);
    EXPECT_EQ(persistedInt(*mc, Layout::kInsertsHeightKey), 90);
}

TEST_F(MixerSectionPanelTest, DividerHoverLightsTheSameDividerInEveryColumnAndTheRail) {
    openMixer(2);
    setDockHeight(600);
    auto& divider = strips()[0]->getSectionDividerForTest(MixerSection::Sends);
    EXPECT_TRUE(divider.getMouseCursor() == juce::MouseCursor::UpDownResizeCursor);
    const auto centre = divider.getLocalBounds().getCentre().toFloat();
    divider.mouseEnter(eventAt(divider, centre, centre));
    EXPECT_EQ(layout().getHoveredDivider(), (int)MixerSection::Sends);
    divider.mouseExit(eventAt(divider, centre, centre));
    EXPECT_EQ(layout().getHoveredDivider(), -1);
    EXPECT_FALSE(strips()[0]->getSectionDividerForTest(MixerSection::Eq).getMouseCursor() ==
                 juce::MouseCursor::UpDownResizeCursor)
        << "the EQ curve has one fixed height, so its divider does not resize";
}

TEST_F(MixerSectionPanelTest, DraggingPastTheFaderMinimumGrowsTheBottomDockAndShrinkingDoesNot) {
    openMixer(2);
    const int dockBefore = mc->getBottomDock().getHeight();
    ASSERT_EQ(dockBefore, 220) << "the dock opens at its default height";
    const int panelBefore = panel().getHeight();
    ASSERT_LT(panelBefore, layout().requiredColumnHeight())
        << "at the default dock the fader is already at its minimum";
    EXPECT_EQ(strips()[0]->getFaderForTest().getHeight(), Layout::kMinFaderHeight);

    DividerDrag drag(strips()[0]->getSectionDividerForTest(MixerSection::Inserts));
    drag.moveBy(36); // two more insert rows: 126
    ASSERT_EQ(layout().getRequestedHeight(MixerSection::Inserts), 126);
    const int required = layout().requiredColumnHeight();
    EXPECT_EQ(panel().getHeight(), required) << "the dock grew by exactly what the sections needed";
    EXPECT_EQ(mc->getBottomDock().getHeight(), dockBefore + (required - panelBefore));
    for (auto* column : strips()) {
        EXPECT_EQ(column->getSectionViewportForTest(MixerSection::Inserts).getHeight(), 126);
        EXPECT_EQ(column->getFaderForTest().getHeight(), Layout::kMinFaderHeight);
    }
    EXPECT_EQ(mc->getAppPropertiesForTest().getUserSettings()->getIntValue(MainComponent::kTimelinePanelHeightKey, -1),
              -1)
        << "the dock height persists on release, not per step";

    drag.release(36);
    const int grownDock = mc->getBottomDock().getHeight();
    EXPECT_EQ(mc->getAppPropertiesForTest().getUserSettings()->getIntValue(MainComponent::kTimelinePanelHeightKey, -1),
              grownDock);

    DividerDrag shrink(strips()[0]->getSectionDividerForTest(MixerSection::Inserts));
    shrink.moveBy(-54); // back to 72
    shrink.release(-54);
    EXPECT_EQ(layout().getRequestedHeight(MixerSection::Inserts), 72);
    EXPECT_EQ(mc->getBottomDock().getHeight(), grownDock) << "shrinking gives the space to the fader, not back";
    EXPECT_EQ(strips()[0]->getFaderForTest().getHeight(), Layout::kMinFaderHeight + 54);
}

TEST_F(MixerSectionPanelTest, AHostThatCannotGrowScrollsTheWholeColumnInstead) {
    openMixer(2);
    panel().canGrowHost = [] { return false; }; // a detached window
    panel().setSize(1200, 250);
    panel().resized();
    auto* content = panel().getViewportForTest().getViewedComponent();
    ASSERT_NE(content, nullptr);
    EXPECT_EQ(content->getHeight(), layout().requiredColumnHeight()) << "every section keeps its full height";
    EXPECT_EQ(strips()[0]->getHeight(), layout().requiredColumnHeight());
    EXPECT_EQ(strips()[0]->getSectionViewportForTest(MixerSection::Inserts).getHeight(), 90);
    EXPECT_TRUE(panel().getViewportForTest().isVerticalScrollBarShown());
}

TEST_F(MixerSectionPanelTest, AListLongerThanItsSectionScrollsWithTheWheelInsideTheSection) {
    openMixer(2);
    setDockHeight(600);
    const int baseInserts = strips()[0]->getInsertListForTest().getEntryCountForTest();
    ASSERT_LE(baseInserts, 4) << "a fresh strip's own chain fits the default 5-row section";
    addInserts(0, 7 - baseInserts);
    auto s = strips();
    ASSERT_EQ(s[0]->getInsertListForTest().getEntryCountForTest(), 7);
    auto& longList = s[0]->getSectionViewportForTest(MixerSection::Inserts);
    auto& shortList = s[1]->getSectionViewportForTest(MixerSection::Inserts);
    EXPECT_TRUE(longList.isScrollable()) << "7 rows in a 5-row section";
    EXPECT_FALSE(shortList.isScrollable());
    EXPECT_EQ(longList.getViewPositionY(), 0);

    auto& list = s[0]->getInsertListForTest();
    const auto pos = list.getLocalBounds().getCentre().toFloat();
    juce::MouseWheelDetails wheel{};
    wheel.deltaY = -0.5f;
    list.mouseWheelMove(eventAt(list, pos, pos), wheel);
    EXPECT_GT(longList.getViewPositionY(), 0) << "the wheel scrolled the list inside its section";
    EXPECT_EQ(s[0]->getSectionViewportForTest(MixerSection::Inserts).getHeight(), 90)
        << "the section itself keeps the shared height";

    auto& fits = s[1]->getInsertListForTest();
    fits.mouseWheelMove(eventAt(fits, pos, pos), wheel);
    EXPECT_EQ(shortList.getViewPositionY(), 0);
}

TEST_F(MixerSectionPanelTest, SectionControlsNeverTakeKeyboardFocusFromThePanel) {
    openMixer(1);
    setDockHeight(600);
    auto* column = strips()[0];
    EXPECT_TRUE(panel().getWantsKeyboardFocus());
    EXPECT_FALSE(panel().getViewportForTest().getWantsKeyboardFocus());
    for (auto section : {MixerSection::Inserts, MixerSection::Sends, MixerSection::Eq}) {
        EXPECT_FALSE(column->getSectionDividerForTest(section).getWantsKeyboardFocus());
        EXPECT_FALSE(column->getCollapsedSectionForTest(section).getWantsKeyboardFocus());
        EXPECT_FALSE(panel().getSectionRailForTest().getToggleButtonForTest(section).getWantsKeyboardFocus());
        EXPECT_FALSE(panel().getSectionRailForTest().getDividerForTest(section).getWantsKeyboardFocus());
    }
    EXPECT_FALSE(column->getSectionViewportForTest(MixerSection::Inserts).getWantsKeyboardFocus());
    EXPECT_FALSE(column->getSectionViewportForTest(MixerSection::Sends).getWantsKeyboardFocus());
}

TEST_F(MixerSectionPanelTest, SectionsPersistAcrossAppLaunches) {
    openMixer(1);
    setDockHeight(700);
    click(panel().getSectionRailForTest().getToggleButtonForTest(MixerSection::Sends));
    DividerDrag drag(strips()[0]->getSectionDividerForTest(MixerSection::Inserts));
    drag.moveBy(18);
    drag.release(18);
    ASSERT_EQ(layout().getRequestedHeight(MixerSection::Inserts), 108);
    mc.reset();

    openMixer(1);
    EXPECT_EQ(layout().getRequestedHeight(MixerSection::Inserts), 108);
    EXPECT_TRUE(layout().isHidden(MixerSection::Sends));
    EXPECT_FALSE(layout().isHidden(MixerSection::Eq));
    EXPECT_TRUE(strips()[0]->getCollapsedSectionForTest(MixerSection::Sends).isVisible());
}

// Not a check: renders a docked mixer with four columns of different insert/send counts to a PNG for
// a visual comparison with the design. Run with --gtest_also_run_disabled_tests; the output path is
// $MIXER_SECTIONS_RENDER_PATH, else mixer-sections.png in the temp directory.
TEST_F(MixerSectionPanelTest, DISABLED_RenderMixerSectionsToPng) {
    openMixer(3);
    setDockHeight(560);
    addInserts(0, 2);
    addInserts(1, 7);
    strips()[2]->getInsertListForTest().addModule("Parametric EQ");
    panel().rebuild();
    const auto bus = panel().createBus();
    panel().rebuild();
    strips()[0]->getSendListForTest().addSendTo(bus);
    panel().rebuild();
    strips()[1]->getSendListForTest().addSendTo(bus);
    panel().rebuild();

    synth::theme::AppLookAndFeel laf;
    panel().setLookAndFeel(&laf);
    panel().setSize(74 + 6 * 144, panel().getHeight());
    panel().resized();
    juce::Image image(juce::Image::ARGB, panel().getWidth(), panel().getHeight(), true, juce::SoftwareImageType());
    {
        juce::Graphics g(image);
        panel().paintEntireComponent(g, true);
    }
    panel().setLookAndFeel(nullptr);

    const auto envPath = juce::SystemStats::getEnvironmentVariable("MIXER_SECTIONS_RENDER_PATH", {});
    const auto file =
        envPath.isNotEmpty()
            ? juce::File(envPath)
            : juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("mixer-sections.png");
    file.deleteFile();
    juce::FileOutputStream out(file);
    ASSERT_TRUE(out.openedOk());
    juce::PNGImageFormat png;
    ASSERT_TRUE(png.writeImageToStream(image, out));
}
