// SidePaneTests.cpp: the reusable side-pane container -- the toggle button, the width drag and its
// 160-320 px clamp, per-tab persistence, the hidden button of a pane without content, the motion
// timings, and that the pane travels with a detached panel. Uses a bare SidePane with a fake content
// and a temporary settings file, plus a real off-screen MainComponent for the detached case.
#include "../../Mixer/MixerZonesTestRig.h"
#include "UI/Layout/SidePane/SidePane.h"
#include "UI/Layout/SidePane/SidePaneToggleButton.h"
#include <gtest/gtest.h>

namespace {

using synth::ui::SidePane;
using synth::ui::SidePaneContent;
using synth::ui::SidePaneToggleButton;

class FakeContent : public SidePaneContent {
public:
    juce::Component& getPaneComponent() override { return component; }
    juce::String getPaneTitle() const override { return "Fake pane"; }
    juce::Component component;
};

struct TempSettings {
    juce::File file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("sidepane-" + juce::Uuid().toString() + ".settings");
    juce::PropertiesFile::Options options() const {
        juce::PropertiesFile::Options o;
        o.storageFormat = juce::PropertiesFile::storeAsXML;
        return o;
    }
    ~TempSettings() { file.deleteFile(); }
};

// The grab-edge gesture, with the pointer measured in the pane's own coordinates.
void dragEdge(SidePane& pane, float from, float to) {
    auto& edge = pane.getGrabEdgeForTest();
    auto at = [&](float x) { return edge.getLocalPoint(&pane, juce::Point<float>(x, 20.0f)); };
    edge.mouseDown(makeClickEvent(edge, at(from)));
    edge.mouseDrag(makeDragEvent(edge, at(to), at(from)));
    edge.mouseUp(makeClickEvent(edge, at(to)));
}

} // namespace

TEST(SidePaneTests, OpensAtTwoHundredPixelsWhenNothingIsStored) {
    SidePane pane;
    FakeContent content;
    pane.setContent(&content);
    EXPECT_TRUE(pane.isOpen());
    EXPECT_EQ(pane.getContentWidth(), SidePane::kDefaultWidth);
    EXPECT_EQ(pane.getOccupiedWidth(), 200);
}

TEST(SidePaneTests, TheToggleButtonShowsAndHidesThePane) {
    SidePane pane;
    FakeContent content;
    SidePaneToggleButton button;
    button.bind(&pane);
    pane.setContent(&content);
    int layouts = 0;
    pane.onOccupiedWidthChanged = [&] { ++layouts; };

    EXPECT_TRUE(button.isVisible());
    EXPECT_TRUE(button.getToggleState());
    button.onClick();
    EXPECT_FALSE(pane.isOpen());
    EXPECT_EQ(pane.getOccupiedWidth(), 0) << "an off-screen pane lands at once";
    EXPECT_FALSE(pane.isAnimating());
    EXPECT_FALSE(button.getToggleState());
    EXPECT_GT(layouts, 0) << "the owner is told to lay out again";
    EXPECT_TRUE(button.getTooltip().startsWith("Show side pane"));

    button.onClick();
    EXPECT_TRUE(pane.isOpen());
    EXPECT_EQ(pane.getOccupiedWidth(), 200);
    EXPECT_TRUE(button.getTooltip().startsWith("Hide side pane"));
}

TEST(SidePaneTests, ATabWithNoPaneContentHidesTheButtonAndOccupiesNoWidth) {
    SidePane pane;
    SidePaneToggleButton button;
    button.bind(&pane);
    EXPECT_FALSE(button.isVisible());
    EXPECT_EQ(pane.getOccupiedWidth(), 0);
    EXPECT_FALSE(pane.isVisible());

    FakeContent content;
    pane.setContent(&content);
    EXPECT_TRUE(button.isVisible());
    pane.setContent(nullptr);
    EXPECT_FALSE(button.isVisible());
    EXPECT_EQ(pane.getOccupiedWidth(), 0);
}

TEST(SidePaneTests, TheShortcutTextJoinsTheTooltip) {
    SidePane pane;
    FakeContent content;
    SidePaneToggleButton button;
    button.bind(&pane);
    pane.setContent(&content);
    button.setShortcutText("Cmd+Shift+B");
    EXPECT_TRUE(button.getTooltip().contains("Cmd+Shift+B"));
}

TEST(SidePaneTests, DraggingTheRightEdgeResizesWithinTheClamp) {
    SidePane pane;
    FakeContent content;
    pane.setContent(&content);
    pane.setBounds(0, 0, pane.getOccupiedWidth(), 300);

    dragEdge(pane, 197.0f, 237.0f);
    EXPECT_EQ(pane.getContentWidth(), 240);

    dragEdge(pane, 237.0f, 900.0f);
    EXPECT_EQ(pane.getContentWidth(), SidePane::kMaxWidth);

    dragEdge(pane, 317.0f, -600.0f);
    EXPECT_EQ(pane.getContentWidth(), SidePane::kMinWidth);
    EXPECT_EQ(pane.getOccupiedWidth(), SidePane::kMinWidth);
}

TEST(SidePaneTests, OpenStateAndWidthPersistPerTab) {
    TempSettings temp;
    {
        juce::PropertiesFile settings(temp.file, temp.options());
        SidePane mixer;
        SidePane other;
        FakeContent a, b;
        mixer.setContent(&a);
        other.setContent(&b);
        mixer.setPersistence(&settings, "mixer");
        other.setPersistence(&settings, "other");

        mixer.setBounds(0, 0, mixer.getOccupiedWidth(), 300);
        dragEdge(mixer, 197.0f, 277.0f); // 200 -> 280; the width is stored when the drag ends
        mixer.setOpen(false);
        EXPECT_TRUE(other.isOpen());
        EXPECT_EQ(other.getContentWidth(), 200) << "another tab's pane is untouched";
    }
    juce::PropertiesFile reloaded(temp.file, temp.options());
    SidePane mixer;
    SidePane other;
    FakeContent a, b;
    mixer.setContent(&a);
    other.setContent(&b);
    mixer.setPersistence(&reloaded, "mixer");
    other.setPersistence(&reloaded, "other");
    EXPECT_FALSE(mixer.isOpen());
    EXPECT_EQ(mixer.getOccupiedWidth(), 0);
    EXPECT_EQ(mixer.getContentWidth(), 280);
    EXPECT_TRUE(other.isOpen());
    EXPECT_EQ(other.getContentWidth(), 200);
}

TEST(SidePaneTests, AStoredWidthOutsideTheRangeIsClamped) {
    TempSettings temp;
    juce::PropertiesFile settings(temp.file, temp.options());
    settings.setValue(SidePane::widthKeyFor("mixer"), 9000);
    SidePane pane;
    FakeContent content;
    pane.setContent(&content);
    pane.setPersistence(&settings, "mixer");
    EXPECT_EQ(pane.getContentWidth(), SidePane::kMaxWidth);
}

TEST(SidePaneTests, MotionFollowsTheMotionRules) {
    EXPECT_DOUBLE_EQ(SidePane::kOpenMs, 160.0);
    EXPECT_DOUBLE_EQ(SidePane::kCloseMs, 110.0);
    EXPECT_DOUBLE_EQ(SidePane::tweenDurationMs(true, 1.0f), 160.0);
    EXPECT_DOUBLE_EQ(SidePane::tweenDurationMs(false, 1.0f), 110.0);
    EXPECT_DOUBLE_EQ(SidePane::tweenDurationMs(true, 0.5f), 80.0) << "a retarget travels only what is left";
    EXPECT_GE(SidePane::tweenDurationMs(false, 0.0001f), 1.0);
}

TEST(SidePaneTests, TheMixerPaneTravelsWithTheMixerIntoADetachedWindow) {
    MixerZonesRig r(1);
    auto& dock = r.mc.getBottomDock();
    auto& panel = *r.panel;
    ASSERT_TRUE(panel.getSidePane().hasContent());
    ASSERT_TRUE(panel.getSidePaneButton().isVisible());
    panel.getSidePane().setOpen(false);

    dock.getMixerHost().setDetached(true);
    ASSERT_TRUE(dock.getMixerHost().isDetached());
    EXPECT_EQ(panel.getSidePane().getParentComponent(), &panel) << "the pane is part of the panel";
    EXPECT_EQ(panel.getTopLevelComponent(),
              static_cast<juce::Component*>(dock.getMixerHost().getDetachedWindowForTest()))
        << "and the panel is now inside the window";
    EXPECT_FALSE(panel.getSidePane().isOpen()) << "with its state";
    panel.getSidePane().setOpen(true);
    EXPECT_EQ(panel.getSidePane().getOccupiedWidth(), 200);
    EXPECT_TRUE(panel.getSidePaneButton().isVisible());

    dock.getMixerHost().setDetached(false);
    EXPECT_EQ(panel.getSidePane().getParentComponent(), &panel);
    EXPECT_TRUE(panel.getSidePane().isOpen());
}

TEST(SidePaneTests, TheMixersPaneStateIsRememberedAppWideAcrossLaunches) {
    BottomDockActiveTabResetGuardMDT resetGuard; // also clears the side-pane keys before and after
    {
        MainComponent first(std::make_unique<MockProviderMZT>());
        first.setSize(1400, 900);
        first.newPatchForTest();
        auto& pane = first.getBottomDock().getMixerPanel().getSidePane();
        ASSERT_TRUE(pane.isOpen()) << "open the first time the Mixer is used";
        EXPECT_EQ(pane.getContentWidth(), 200);
        dragEdge(pane, 197.0f, 257.0f);
        pane.setOpen(false);
    }
    MainComponent second(std::make_unique<MockProviderMZT>());
    second.setSize(1400, 900);
    second.newPatchForTest();
    auto& pane = second.getBottomDock().getMixerPanel().getSidePane();
    EXPECT_FALSE(pane.isOpen());
    EXPECT_EQ(pane.getContentWidth(), 260);
    EXPECT_EQ(pane.getOccupiedWidth(), 0);
}

TEST(SidePaneTests, TheMixerPanelLaysColumnsOutBesideThePane) {
    MixerZonesRig r(2);
    auto& pane = r.panel->getSidePane();
    ASSERT_TRUE(pane.isOpen());
    EXPECT_EQ(pane.getX(), 0);
    EXPECT_EQ(pane.getWidth(), 200);
    EXPECT_EQ(r.panel->getViewportForTest().getX(), 200);

    r.panel->getSidePaneButton().onClick();
    EXPECT_EQ(pane.getWidth(), 0);
    EXPECT_FALSE(pane.isVisible());
    EXPECT_EQ(r.panel->getViewportForTest().getX(), 0) << "closing gives the width back to the columns";
    // The button is at the LEFT end of the toolbar, before the section toggles.
    EXPECT_LT(r.panel->getSidePaneButton().getX(),
              r.panel->getToolbarForTest().getSectionToggleForTest(synth::ui::MixerSection::Inserts).getX());
}
