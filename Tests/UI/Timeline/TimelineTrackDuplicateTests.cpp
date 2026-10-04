// TimelineTrackDuplicateTests.cpp
//
// Duplicating a track from its header row: Cmd+D (the rebindable "timelineDuplicateFocusedTrack") driven through
// the row's real keyPressed(), the right-click menu's "Duplicate Track" entry, and the row glide a duplicate arms.
// What the host does with the request is MainComponentDuplicateTrackTests.cpp's concern; here the host is a stub
// that records the call.

#include "ShortcutManager/ShortcutManager.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent.h"
#include <gtest/gtest.h>

using synth::TimelineDoc;
using synth::TrackId;
using synth::TrackKind;
using synth::ui::TimelineTrackHeaderComponent;
using synth::ui::TrackHeaderHost;

namespace {

juce::KeyPress cmdD() { return juce::KeyPress('d', juce::ModifierKeys::commandModifier, 0); }
juce::KeyPress plainD() { return juce::KeyPress('d', juce::ModifierKeys::noModifiers, 0); }

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
    void duplicateTrack(TrackId track) override { duplicated.push_back(track); }

    std::vector<TrackId> duplicated;
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

const juce::PopupMenu::Item* findItem(const juce::PopupMenu& menu, const juce::String& text) {
    juce::PopupMenu::MenuItemIterator it(menu, true);
    while (it.next())
        if (it.getItem().text == text)
            return &it.getItem();
    return nullptr;
}

struct ReducedMotionGuard {
    ~ReducedMotionGuard() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

} // namespace

TEST(TimelineTrackDuplicateTest, CmdDOnAFocusedRowAsksTheHostToDuplicateThatTrack) {
    Fixture f;
    f.doc.addTrack(TrackKind::Midi, "A");
    const auto b = f.doc.addTrack(TrackKind::Midi, "B");
    auto* header = f.panel.getTrackHeaderAt(1);
    ASSERT_NE(header, nullptr);

    EXPECT_TRUE(header->keyPressed(cmdD()));
    ASSERT_EQ(f.host.duplicated.size(), 1u);
    EXPECT_EQ(f.host.duplicated[0], b);
}

TEST(TimelineTrackDuplicateTest, OtherKeysAreNotClaimed) {
    Fixture f;
    f.doc.addTrack(TrackKind::Midi, "A");
    auto* header = f.panel.getTrackHeaderAt(0);
    ASSERT_NE(header, nullptr);

    EXPECT_FALSE(header->keyPressed(plainD()));
    EXPECT_TRUE(f.host.duplicated.empty());
}

TEST(TimelineTrackDuplicateTest, TheKeyIsRebindableThroughTheShortcutManager) {
    Fixture f;
    f.doc.addTrack(TrackKind::Midi, "A");
    ShortcutManager manager;
    manager.setBinding("timelineDuplicateFocusedTrack", juce::KeyPress('u', juce::ModifierKeys::commandModifier, 0));
    f.panel.setShortcutManager(&manager);
    auto* header = f.panel.getTrackHeaderAt(0);
    ASSERT_NE(header, nullptr);

    EXPECT_FALSE(header->keyPressed(cmdD())) << "the default chord was rebound away";
    EXPECT_TRUE(header->keyPressed(juce::KeyPress('u', juce::ModifierKeys::commandModifier, 0)));
    EXPECT_EQ(f.host.duplicated.size(), 1u);

    f.panel.setShortcutManager(nullptr); // before `manager` goes out of scope
}

TEST(TimelineTrackDuplicateTest, TheAutomationSectionHeaderLetsTheKeyBubble) {
    Fixture f;
    f.doc.addTrack(TrackKind::Automation, "Automation");
    auto* header = f.panel.getTrackHeaderAt(0);
    ASSERT_NE(header, nullptr);
    ASSERT_TRUE(header->isSectionHeader());

    EXPECT_FALSE(header->keyPressed(cmdD()));
    EXPECT_TRUE(f.host.duplicated.empty());
}

TEST(TimelineTrackDuplicateTest, TheRowMenuOffersDuplicateTrackNamingItsShortcutAndRoutesTheChoice) {
    Fixture f;
    const auto a = f.doc.addTrack(TrackKind::Midi, "A");
    auto* header = f.panel.getTrackHeaderAt(0);
    ASSERT_NE(header, nullptr);

    const auto menu = header->buildContextMenu();
    const auto* item = findItem(menu, "Duplicate Track");
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->itemID, TimelineTrackHeaderComponent::kDuplicateTrackMenuId);
    EXPECT_EQ(item->shortcutKeyDescription, cmdD().getTextDescriptionWithIcons());

    header->applyContextMenuChoice(item->itemID);
    ASSERT_EQ(f.host.duplicated.size(), 1u);
    EXPECT_EQ(f.host.duplicated[0], a);
}

TEST(TimelineTrackDuplicateTest, TheAutomationSectionMenuHasNoDuplicateEntry) {
    Fixture f;
    f.doc.addTrack(TrackKind::Automation, "Automation");
    auto* header = f.panel.getTrackHeaderAt(0);
    ASSERT_NE(header, nullptr);
    EXPECT_EQ(findItem(header->buildContextMenu(), "Duplicate Track"), nullptr);
}

TEST(TimelineTrackDuplicateTest, TheCopyGrowsOutOfItsSourceRowWhenTheRowsCanGlide) {
    ReducedMotionGuard guard;
    synth::ui::setReducedMotionForTest(false);
    Fixture f;
    f.panel.forceTrackGlideForTest(true);
    const auto a = f.doc.addTrack(TrackKind::Midi, "A");
    f.doc.addTrack(TrackKind::Midi, "B");
    ASSERT_FALSE(f.panel.isTrackReorderActiveForTest());

    f.panel.armTrackDuplicateGlide(a);
    ASSERT_TRUE(f.doc.duplicateTrack(a, "A copy", 0xff123456, {}).isValid());

    EXPECT_TRUE(f.panel.isTrackReorderActiveForTest()) << "the rows below glide down and the new row emerges";
    ASSERT_EQ(f.panel.getTrackHeaderCount(), 3);
}

TEST(TimelineTrackDuplicateTest, UnderReduceMotionTheCopyLandsAtOnce) {
    ReducedMotionGuard guard;
    synth::ui::setReducedMotionForTest(true);
    Fixture f;
    f.panel.forceTrackGlideForTest(true);
    const auto a = f.doc.addTrack(TrackKind::Midi, "A");
    f.doc.addTrack(TrackKind::Midi, "B");

    f.panel.armTrackDuplicateGlide(a);
    ASSERT_TRUE(f.doc.duplicateTrack(a, "A copy", 0xff123456, {}).isValid());

    EXPECT_FALSE(f.panel.isTrackReorderActiveForTest());
    EXPECT_EQ(f.panel.getTrackHeaderCount(), 3);
}

TEST(TimelineTrackDuplicateTest, WithoutAnArmTheRebuildNeverGlides) {
    ReducedMotionGuard guard;
    synth::ui::setReducedMotionForTest(false);
    Fixture f;
    f.panel.forceTrackGlideForTest(true);
    const auto a = f.doc.addTrack(TrackKind::Midi, "A");

    ASSERT_TRUE(f.doc.duplicateTrack(a, "A copy", 0xff123456, {}).isValid());
    EXPECT_FALSE(f.panel.isTrackReorderActiveForTest());
}
