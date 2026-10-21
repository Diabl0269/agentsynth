// TimelineClipLaneLinkedClipsTests.cpp — the clip lane's linked-clip surface: the context menu's
// "Duplicate linked" / "Make unique", the Shift+Cmd+D key, the link mark and its tooltip, and the
// screen-reader text.
#include "TimelineClipLaneTestFixture.h"

#include "ShortcutManager/ShortcutManager.h"
#include "UI/Timeline/ClipAccessibilityText.h"

namespace {

// The menu item named `text`, or nullptr; `found` keeps the copy alive for the caller.
const juce::PopupMenu::Item* findItem(const juce::PopupMenu& menu, const juce::String& text,
                                      juce::PopupMenu::Item& found) {
    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();) {
        if (it.getItem().text == text) {
            found = it.getItem();
            return &found;
        }
    }
    return nullptr;
}

juce::KeyPress shiftCmdD() {
    return juce::KeyPress('d', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);
}

juce::Image paintLane(TimelineClipLaneArea& lane) {
    juce::Image image(juce::Image::ARGB, lane.getWidth(), lane.getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g(image);
    lane.paintEntireComponent(g, false);
    return image;
}

} // namespace

TEST(TimelineClipLaneLinkedClipsTest, MenuOffersDuplicateLinkedAndMakeUniqueOnlyWhereTheyApply) {
    ClipLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "Drums");
    const auto clip = f.doc.addClip(track, 0.0, 4.0, "Beat");
    f.doc.addNote(clip, makeNote(0.0, 36));
    const auto pos = f.lane.getClipRect(clip).getCentre();

    juce::PopupMenu::Item item;
    auto menu = f.lane.buildClipContextMenu(clip, pos);
    const auto* dupLinked = findItem(menu, "Duplicate linked", item);
    ASSERT_NE(dupLinked, nullptr);
    EXPECT_TRUE(dupLinked->isEnabled);
    EXPECT_TRUE(dupLinked->shortcutKeyDescription.isNotEmpty()) << "the menu names the key";
    EXPECT_NE(findItem(menu, "Duplicate", item), nullptr) << "plain Duplicate stays";
    EXPECT_EQ(findItem(menu, "Make unique", item), nullptr) << "nothing to make unique yet";

    // Running the real item links a copy, as one undo step.
    findItem(menu, "Duplicate linked", item);
    ASSERT_TRUE(item.action != nullptr);
    item.action();
    const auto& clips = f.doc.getTrack(track)->clips;
    ASSERT_EQ(clips.size(), 2u);
    EXPECT_EQ(f.doc.countLinkedClips(clip), 1);
    EXPECT_TRUE(f.selection.contains(clips[1].id)) << "the copy becomes the selection";

    menu = f.lane.buildClipContextMenu(clip, pos);
    ASSERT_NE(findItem(menu, "Make unique", item), nullptr);
    item.action();
    EXPECT_EQ(f.doc.countLinkedClips(clip), 0);

    ASSERT_TRUE(f.undo.undo());
    EXPECT_EQ(f.doc.countLinkedClips(clip), 1) << "Make unique was one step";
    ASSERT_TRUE(f.undo.undo());
    EXPECT_EQ(f.doc.getTrack(track)->clips.size(), 1u) << "Duplicate linked was one step";
}

TEST(TimelineClipLaneLinkedClipsTest, AudioClipGreysOutDuplicateLinked) {
    ClipLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Audio, "Vox");
    const auto clip = f.doc.addClip(track, 0.0, 4.0, "Take");
    ASSERT_TRUE(f.doc.setClipAsset(clip, "Audio/take.wav", 0.0));

    juce::PopupMenu::Item item;
    const auto menu = f.lane.buildClipContextMenu(clip, f.lane.getClipRect(clip).getCentre());
    const auto* dupLinked = findItem(menu, "Duplicate linked", item);
    ASSERT_NE(dupLinked, nullptr);
    EXPECT_FALSE(dupLinked->isEnabled);

    f.lane.applyClipContextChoice(clip, TimelineClipLaneArea::ClipContextChoice::DuplicateLinked, 0.0);
    EXPECT_EQ(f.doc.getTrack(track)->clips.size(), 1u);
    EXPECT_FALSE(f.undo.canUndo());
}

TEST(TimelineClipLaneLinkedClipsTest, ShiftCmdDLinksEverySelectedClipAsOneTiledBlock) {
    ClipLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "Drums");
    const auto a = f.doc.addClip(track, 0.0, 4.0, "A");
    const auto b = f.doc.addClip(track, 4.0, 4.0, "B");
    f.selection.setSelection({a, b});

    ASSERT_TRUE(f.lane.keyPressed(shiftCmdD()));
    const auto& clips = f.doc.getTrack(track)->clips;
    ASSERT_EQ(clips.size(), 4u);
    EXPECT_DOUBLE_EQ(clips[2].startBeat, 8.0) << "copies tile after the selection, not on top of B";
    EXPECT_DOUBLE_EQ(clips[3].startBeat, 12.0);
    EXPECT_EQ(clips[2].linkGroup, f.doc.getClip(a)->linkGroup);
    EXPECT_EQ(clips[3].linkGroup, f.doc.getClip(b)->linkGroup);
    EXPECT_NE(clips[2].linkGroup, clips[3].linkGroup) << "each clip links to its own copy";
    EXPECT_TRUE(f.selection.contains(clips[2].id));

    ASSERT_TRUE(f.undo.undo());
    EXPECT_EQ(f.doc.getTrack(track)->clips.size(), 2u);
    EXPECT_FALSE(f.undo.canUndo());

    f.selection.clear();
    EXPECT_FALSE(f.lane.keyPressed(shiftCmdD())) << "nothing selected: the key falls through";
}

TEST(TimelineClipLaneLinkedClipsTest, DuplicateLinkedIsARebindableTimelineShortcut) {
    ShortcutManager manager;
    EXPECT_EQ(manager.getBinding("timelineClipDuplicateLinked"), shiftCmdD());
    EXPECT_EQ(ShortcutManager::getCategory("timelineClipDuplicateLinked"), ShortcutCategory::Timeline);
    EXPECT_TRUE(ShortcutManager::getActionDescription("timelineClipDuplicateLinked").isNotEmpty());
    EXPECT_TRUE(manager.getConflictingAction("timelineClipDuplicateLinked", shiftCmdD()).isEmpty());

    // The lane follows a rebind.
    ClipLaneFixture f;
    f.lane.setShortcutManager(&manager);
    const auto track = f.doc.addTrack(TrackKind::Midi, "Drums");
    const auto a = f.doc.addClip(track, 0.0, 4.0, "A");
    f.selection.setSelection({a});
    const juce::KeyPress rebound('l', juce::ModifierKeys::altModifier, 0);
    manager.setBinding("timelineClipDuplicateLinked", rebound);
    EXPECT_FALSE(f.lane.keyPressed(shiftCmdD()));
    EXPECT_TRUE(f.lane.keyPressed(rebound));
    EXPECT_EQ(f.doc.countLinkedClips(a), 1);
}

TEST(TimelineClipLaneLinkedClipsTest, LinkedClipPaintsAMarkWithATooltip) {
    ClipLaneFixture f;
    const auto track = f.doc.addTrack(TrackKind::Midi, "Drums");
    const auto a = f.doc.addClip(track, 0.0, 4.0, "Beat");
    const auto mark = TimelineClipLaneArea::linkMarkBounds(f.lane.getClipRect(a));
    ASSERT_FALSE(mark.isEmpty());

    const auto unlinked = paintLane(f.lane);
    EXPECT_TRUE(f.lane.linkTooltipAt(mark.getCentre()).isEmpty());

    ASSERT_TRUE(f.doc.duplicateClipLinked(a).isValid()); // the copy lands at beat 4, clear of a's mark
    const auto linked = paintLane(f.lane);

    bool markChanged = false;
    for (int y = mark.getY(); y < mark.getBottom(); ++y)
        for (int x = mark.getX(); x < mark.getRight(); ++x)
            markChanged |= unlinked.getPixelAt(x, y) != linked.getPixelAt(x, y);
    EXPECT_TRUE(markChanged) << "the link mark is drawn in the clip's name strip";

    EXPECT_EQ(f.lane.linkTooltipAt(mark.getCentre()),
              "Linked with 1 other clip: editing the notes of one changes them all. Right-click > Make unique to "
              "edit this one alone.");
    EXPECT_TRUE(f.lane.linkTooltipAt(f.lane.getClipRect(a).getCentre()).isEmpty()) << "only on the mark";
}

TEST(TimelineClipLaneLinkedClipsTest, ScreenReaderTextSaysHowManyClipsAreLinked) {
    synth::Track track;
    track.name = "Drums";
    synth::Clip clip;
    clip.name = "Beat";
    clip.startBeat = 0.0;
    clip.lengthBeats = 4.0;
    EXPECT_EQ(synth::ui::describeClipForAccessibility(clip, track, 4.0, 1),
              "MIDI clip Beat, track Drums, bars 1 to 2, linked with 1 other clip");
    EXPECT_EQ(synth::ui::describeClipForAccessibility(clip, track, 4.0, 3),
              "MIDI clip Beat, track Drums, bars 1 to 2, linked with 3 other clips");

    ClipLaneFixture f;
    const auto trackId = f.doc.addTrack(TrackKind::Midi, "Drums");
    const auto a = f.doc.addClip(trackId, 0.0, 4.0, "Beat");
    f.doc.duplicateClipLinked(a);
    f.lane.setKeyboardClip(a);
    EXPECT_TRUE(f.lane.getKeyboardClipAccessibilityText().endsWith(", linked with 1 other clip"));
}
