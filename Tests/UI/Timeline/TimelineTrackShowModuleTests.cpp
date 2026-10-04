// TimelineTrackShowModuleTests.cpp
//
// "Show module" from a track header: Ctrl+E (the rebindable "timelineShowFocusedTrackModule") through the row's real
// keyPressed(), and the header's button through its real click and keyboard paths. What the host does with the request
// (select, centre, open or close the window) is MainComponentShowTrackModuleTests.cpp's concern; here the host is a
// stub that records the call.

#include "ShortcutManager/ShortcutManager.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent.h"
#include <gtest/gtest.h>

using synth::TimelineDoc;
using synth::TrackId;
using synth::TrackKind;
using synth::ui::TimelineTrackHeaderComponent;
using synth::ui::TrackHeaderHost;

namespace {

juce::KeyPress ctrlE() { return juce::KeyPress('e', juce::ModifierKeys::ctrlModifier, 0); }
juce::KeyPress plainE() { return juce::KeyPress('e', juce::ModifierKeys::noModifiers, 0); }

struct RecordingHost : TrackHeaderHost {
    std::vector<BindingOption> getAvailableTrackInNodes(TrackId) override { return {}; }
    juce::String getNodeDisplayName(const juce::String&) override { return {}; }
    void bindTrackTo(TrackId, const juce::String&) override {}
    void createAndBindTrackInNode(TrackId) override {}
    void selectNodeInGraph(const juce::String&) override {}
    void deleteTrack(TrackId) override {}
    void performTrackEdit(const std::function<void()>& mutation) override {
        if (mutation)
            mutation();
    }
    void addMidiTrack() override {}
    void addAudioTrack() override {}
    void addInstrumentTrack(const juce::String&, bool) override {}
    std::vector<PluginLaneOption> getAvailablePluginLaneOptions() const override { return {}; }
    synth::LaneId addPluginAutomationLane(const PluginLaneOption&) override { return {}; }
    void showTrackModule(TrackId track) override { shown.push_back(track); }

    std::vector<TrackId> shown;
};

struct Fixture {
    TimelineDoc doc;
    RecordingHost host;
    synth::ui::TimelinePanelComponent panel;

    Fixture() {
        panel.setSize(1200, 320);
        panel.setTrackHeaderHost(&host);
        panel.setTimelineDoc(&doc);
    }
};

} // namespace

TEST(TimelineTrackShowModuleTest, CtrlEOnAFocusedRowAsksTheHostToShowThatTracksModule) {
    Fixture f;
    f.doc.addTrack(TrackKind::Midi, "A");
    const auto b = f.doc.addTrack(TrackKind::Midi, "B");
    auto* header = f.panel.getTrackHeaderAt(1);
    ASSERT_NE(header, nullptr);

    EXPECT_TRUE(header->keyPressed(ctrlE()));
    ASSERT_EQ(f.host.shown.size(), 1u);
    EXPECT_EQ(f.host.shown[0], b);
}

TEST(TimelineTrackShowModuleTest, OtherKeysAreNotClaimed) {
    Fixture f;
    f.doc.addTrack(TrackKind::Midi, "A");
    auto* header = f.panel.getTrackHeaderAt(0);
    ASSERT_NE(header, nullptr);

    EXPECT_FALSE(header->keyPressed(plainE()));
    EXPECT_TRUE(f.host.shown.empty());
}

TEST(TimelineTrackShowModuleTest, TheKeyIsRebindableThroughTheShortcutManager) {
    Fixture f;
    f.doc.addTrack(TrackKind::Midi, "A");
    ShortcutManager manager;
    manager.setBinding("timelineShowFocusedTrackModule", juce::KeyPress('u', juce::ModifierKeys::ctrlModifier, 0));
    f.panel.setShortcutManager(&manager);
    auto* header = f.panel.getTrackHeaderAt(0);
    ASSERT_NE(header, nullptr);

    EXPECT_FALSE(header->keyPressed(ctrlE())) << "the default chord was rebound away";
    EXPECT_TRUE(header->keyPressed(juce::KeyPress('u', juce::ModifierKeys::ctrlModifier, 0)));
    EXPECT_EQ(f.host.shown.size(), 1u);
    EXPECT_NE(header->getShowModuleButton().getTooltip().indexOfIgnoreCase("U"), -1) << "the tooltip names the binding";

    f.panel.setShortcutManager(nullptr); // before `manager` goes out of scope
}

TEST(TimelineTrackShowModuleTest, TheAutomationSectionHeaderLetsTheKeyBubble) {
    Fixture f;
    f.doc.addTrack(TrackKind::Midi, "A");
    f.doc.addTrack(TrackKind::Automation, "Automation");
    for (int i = 0; i < 2; ++i)
        if (auto* header = f.panel.getTrackHeaderAt(i); header != nullptr && header->isSectionHeader()) {
            EXPECT_FALSE(header->keyPressed(ctrlE()));
            EXPECT_FALSE(header->getShowModuleButton().isVisible());
        }
    EXPECT_TRUE(f.host.shown.empty());
}

TEST(TimelineTrackShowModuleTest, TheButtonShowsTheModuleOnClickAndOnReturnOrSpace) {
    Fixture f;
    const auto a = f.doc.addTrack(TrackKind::Midi, "A");
    auto* header = f.panel.getTrackHeaderAt(0);
    ASSERT_NE(header, nullptr);
    auto& button = header->getShowModuleButton();

    button.triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(30); // triggerClick posts its click
    EXPECT_TRUE(button.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)));
    EXPECT_TRUE(button.keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    ASSERT_EQ(f.host.shown.size(), 3u);
    for (const auto id : f.host.shown)
        EXPECT_EQ(id, a);
}

TEST(TimelineTrackShowModuleTest, TheButtonIsATabStopWithANameAndATooltipNamingTheShortcut) {
    Fixture f;
    f.doc.addTrack(TrackKind::Midi, "Lead");
    auto* header = f.panel.getTrackHeaderAt(0);
    ASSERT_NE(header, nullptr);
    auto& button = header->getShowModuleButton();

    EXPECT_TRUE(button.getWantsKeyboardFocus());
    EXPECT_FALSE(button.getMouseClickGrabsKeyboardFocus()) << "a click must leave focus on the row";
    EXPECT_EQ(button.getTitle(), "Show Lead module");
    EXPECT_TRUE(button.getTooltip().contains("Lead")) << button.getTooltip();
    EXPECT_TRUE(button.getTooltip().containsIgnoreCase("E")) << "names its shortcut: " << button.getTooltip();
}

TEST(TimelineTrackShowModuleTest, TheButtonFitsTheRowAtTheMinimumHeaderWidth) {
    Fixture f;
    f.doc.addTrack(TrackKind::Midi, "A");
    auto* header = f.panel.getTrackHeaderAt(0);
    ASSERT_NE(header, nullptr);
    header->setSize(synth::ui::TimelinePanelComponent::kMinTrackHeaderWidth, 56);
    auto& button = header->getShowModuleButton();

    EXPECT_TRUE(button.isVisible());
    EXPECT_TRUE(header->getLocalBounds().contains(button.getBounds())) << "overflows the row";
    EXPECT_GT(button.getWidth(), 0);
    EXPECT_FALSE(button.getBounds().intersects(header->getBindingChip().getBounds())) << "overlaps the binding chip";
    EXPECT_GE(header->getBindingChip().getWidth(), 60) << "the chip keeps a readable width";
}
