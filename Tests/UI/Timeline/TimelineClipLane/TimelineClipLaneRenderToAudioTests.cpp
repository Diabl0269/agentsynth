// TimelineClipLaneRenderToAudioTests.cpp -- "Render to audio track" on the clip lanes: the item in the Range tool's
// menu and in a clip's menu (opened by real right-clicks), its disabled text when no track has a mixer channel, the
// spans each hands the host, and the rebindable shortcut. The render itself is MainComponentRenderToAudioTests.cpp.
#include "TimelineClipLaneTestFixture.h"

#include "ShortcutManager/ShortcutManager.h"
#include "UI/Timeline/EditTool.h"

using synth::TrackId;
using synth::ui::EditTool;
using synth::ui::RenderSpan;

namespace {

// Builds the menu a right-click asks for instead of showing it (a real juce::PopupMenu needs a display).
class MenuLane : public TimelineClipLaneArea {
public:
    using TimelineClipLaneArea::TimelineClipLaneArea;
    std::vector<juce::PopupMenu::Item> rangeMenu, clipMenu;

protected:
    void showRangeContextMenu(juce::Point<int>) override { rangeMenu = items(buildRangeContextMenu()); }
    void showClipContextMenu(synth::ClipId id, juce::Point<int> pos) override {
        clipMenu = items(buildClipContextMenu(id, pos));
    }

private:
    static std::vector<juce::PopupMenu::Item> items(juce::PopupMenu menu) {
        std::vector<juce::PopupMenu::Item> out;
        juce::PopupMenu::MenuItemIterator it(menu);
        while (it.next())
            out.push_back(it.getItem());
        return out;
    }
};

struct Fixture {
    TimelineDoc doc;
    TimelineViewState state;
    ClipSelectionModel selection;
    AppUndoManager undo;
    MenuLane lane{state, selection};
    TrackId t0, t1, noChannel;
    std::vector<std::vector<RenderSpan>> requests;
    bool channelsAvailable = true;

    Fixture() {
        state.pixelsPerBeat = 40.0;
        state.snap = TimelineViewState::Snap::Quarter;
        lane.setTimelineDoc(&doc);
        lane.setUndoManager(&undo);
        lane.setSize(1200, 400);
        t0 = doc.addTrack(TrackKind::Midi, "Lead");
        t1 = doc.addTrack(TrackKind::Midi, "Bass");
        noChannel = doc.addTrack(TrackKind::Midi, "Loose");
        // Stands in for the host: every track but "Loose" reaches a channel.
        lane.renderableTracks = [this](const std::vector<TrackId>& tracks) {
            std::vector<TrackId> ok;
            for (const auto id : tracks)
                if (channelsAvailable && id != noChannel)
                    ok.push_back(id);
            return ok;
        };
        lane.onRenderToAudioRequested = [this](std::vector<RenderSpan> spans) { requests.push_back(spans); };
    }

    juce::Point<float> at(double beat, int row) const {
        return {(float)state.beatToX(beat), (float)(row * lane.getRowHeight() + lane.getRowHeight() / 2)};
    }
};

const juce::PopupMenu::Item* find(const std::vector<juce::PopupMenu::Item>& items, const juce::String& text) {
    for (const auto& item : items)
        if (item.text == text)
            return &item;
    return nullptr;
}

// The manager's default for this platform: Shift+Cmd+R on the Mac, Ctrl+Alt+Shift+R elsewhere (Ctrl+Shift+R is Repeat).
const juce::KeyPress shiftCmdR = ShortcutManager().getBinding("timelineRenderToAudioTrack");

} // namespace

TEST(TimelineClipLaneRenderToAudioTest, RangeMenuOffersTheItemOnceARightClickOpensIt) {
    Fixture f;
    f.lane.setActiveTool(EditTool::Range);
    f.doc.addClip(f.t0, 0.0, 8.0, "c");
    f.lane.setRange(f.t0, 2.0, f.t1, 6.0);

    f.lane.mouseDown(rightClick(f.lane, f.at(3.0, 0)));
    const auto* item = find(f.lane.rangeMenu, "Render to audio track");
    ASSERT_NE(item, nullptr) << "the range menu lists Render to audio track";
    EXPECT_TRUE(item->isEnabled);
    EXPECT_TRUE(item->shortcutKeyDescription.isNotEmpty()) << "the shortcut is named beside the item";

    item->action();
    ASSERT_EQ(f.requests.size(), 1u);
    ASSERT_EQ(f.requests[0].size(), 2u) << "one span per covered track";
    EXPECT_EQ(f.requests[0][0].track, f.t0);
    EXPECT_EQ(f.requests[0][1].track, f.t1);
    EXPECT_DOUBLE_EQ(f.requests[0][1].startBeat, 2.0);
    EXPECT_DOUBLE_EQ(f.requests[0][1].endBeat, 6.0);
}

TEST(TimelineClipLaneRenderToAudioTest, ARangeOverATrackWithNoChannelGreysTheItemWithTheReason) {
    Fixture f;
    f.lane.setActiveTool(EditTool::Range);
    f.lane.setRange(f.noChannel, 2.0, f.noChannel, 6.0);
    f.lane.mouseDown(rightClick(f.lane, f.at(3.0, 2)));

    const auto* item = find(f.lane.rangeMenu, "Render to audio track (no mixer channel)");
    ASSERT_NE(item, nullptr);
    EXPECT_FALSE(item->isEnabled);
    EXPECT_EQ(find(f.lane.rangeMenu, "Render to audio track"), nullptr);
}

TEST(TimelineClipLaneRenderToAudioTest, ClipMenuOffersTheItemAndRendersTheClipsSpan) {
    Fixture f;
    const auto clip = f.doc.addClip(f.t0, 4.0, 4.0, "Verse");
    f.lane.mouseDown(rightClick(f.lane, f.at(5.0, 0)));

    const auto* item = find(f.lane.clipMenu, "Render to audio track");
    ASSERT_NE(item, nullptr) << "the clip menu lists Render to audio track";
    EXPECT_TRUE(item->isEnabled);
    item->action();
    ASSERT_EQ(f.requests.size(), 1u);
    ASSERT_EQ(f.requests[0].size(), 1u);
    EXPECT_EQ(f.requests[0][0].track, f.t0);
    EXPECT_DOUBLE_EQ(f.requests[0][0].startBeat, 4.0);
    EXPECT_DOUBLE_EQ(f.requests[0][0].endBeat, 8.0);
    EXPECT_TRUE(f.doc.getClip(clip) != nullptr);
}

TEST(TimelineClipLaneRenderToAudioTest, SeveralSelectedClipsRenderTheirUnionOnEachTrack) {
    Fixture f;
    const auto a = f.doc.addClip(f.t0, 0.0, 2.0, "a");
    const auto b = f.doc.addClip(f.t0, 6.0, 2.0, "b");
    const auto c = f.doc.addClip(f.t1, 1.0, 3.0, "c");
    f.selection.setSelection({a, b, c});
    f.lane.mouseDown(rightClick(f.lane, f.at(1.0, 0)));

    const auto* item = find(f.lane.clipMenu, "Render to audio track");
    ASSERT_NE(item, nullptr);
    item->action();
    ASSERT_EQ(f.requests.size(), 1u);
    ASSERT_EQ(f.requests[0].size(), 2u);
    EXPECT_EQ(f.requests[0][0].track, f.t0);
    EXPECT_DOUBLE_EQ(f.requests[0][0].startBeat, 0.0);
    EXPECT_DOUBLE_EQ(f.requests[0][0].endBeat, 8.0) << "the union of the selected clips on that track";
    EXPECT_EQ(f.requests[0][1].track, f.t1);
    EXPECT_DOUBLE_EQ(f.requests[0][1].endBeat, 4.0);
}

TEST(TimelineClipLaneRenderToAudioTest, ClipMenuGreysTheItemForATrackWithNoChannel) {
    Fixture f;
    f.doc.addClip(f.noChannel, 0.0, 4.0, "x");
    f.lane.mouseDown(rightClick(f.lane, f.at(1.0, 2)));
    const auto* item = find(f.lane.clipMenu, "Render to audio track (no mixer channel)");
    ASSERT_NE(item, nullptr);
    EXPECT_FALSE(item->isEnabled);
}

TEST(TimelineClipLaneRenderToAudioTest, TheShortcutRendersTheRangeElseTheSelectedClips) {
    Fixture f;
    ShortcutManager manager;
    f.lane.setShortcutManager(&manager);
    EXPECT_TRUE(shiftCmdR.isValid());
    EXPECT_EQ(ShortcutManager::getCategory("timelineRenderToAudioTrack"), ShortcutCategory::Timeline);
    EXPECT_TRUE(ShortcutManager::getActionDescription("timelineRenderToAudioTrack").isNotEmpty());
    EXPECT_TRUE(manager.getConflictingAction("timelineRenderToAudioTrack", shiftCmdR).isEmpty())
        << "the default is free in every category";

    EXPECT_FALSE(f.lane.keyPressed(shiftCmdR)) << "no range and no selection: the key falls through";

    const auto clip = f.doc.addClip(f.t0, 2.0, 2.0, "c");
    f.selection.setSelection({clip});
    EXPECT_TRUE(f.lane.keyPressed(shiftCmdR));
    ASSERT_EQ(f.requests.size(), 1u);
    EXPECT_DOUBLE_EQ(f.requests[0][0].startBeat, 2.0);

    f.lane.setActiveTool(EditTool::Range);
    f.lane.setRange(f.t0, 1.0, f.t1, 5.0);
    EXPECT_TRUE(f.lane.keyPressed(shiftCmdR));
    ASSERT_EQ(f.requests.size(), 2u);
    EXPECT_EQ(f.requests[1].size(), 2u) << "a live range wins over the clip selection";

    const juce::KeyPress rebound('k', juce::ModifierKeys::altModifier, 0);
    manager.setBinding("timelineRenderToAudioTrack", rebound);
    EXPECT_FALSE(f.lane.keyPressed(shiftCmdR));
    EXPECT_TRUE(f.lane.keyPressed(rebound));
    EXPECT_EQ(f.requests.size(), 3u);
}

TEST(TimelineClipLaneRenderToAudioTest, WithoutAHostTheItemIsNeverOffered) {
    Fixture f;
    f.lane.onRenderToAudioRequested = nullptr;
    f.doc.addClip(f.t0, 0.0, 4.0, "c");
    f.lane.mouseDown(rightClick(f.lane, f.at(1.0, 0)));
    EXPECT_EQ(find(f.lane.clipMenu, "Render to audio track"), nullptr);
    EXPECT_FALSE(f.lane.keyPressed(shiftCmdR));
}
