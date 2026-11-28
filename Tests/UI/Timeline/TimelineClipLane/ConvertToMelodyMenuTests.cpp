// ConvertToMelodyMenuTests.cpp -- the two "Convert to melody" items on an audio clip's context menu: offered for an
// audio clip when the host listens, never for a MIDI clip, and each hands the host the clip and its snap choice.
#include "TimelineClipLaneTestFixture.h"

using synth::ClipId;

namespace {

class MenuLane : public TimelineClipLaneArea {
public:
    using TimelineClipLaneArea::TimelineClipLaneArea;
    std::vector<juce::PopupMenu::Item> itemsFor(ClipId id) {
        std::vector<juce::PopupMenu::Item> out;
        auto menu = buildClipContextMenu(id, {0, 0});
        juce::PopupMenu::MenuItemIterator it(menu);
        while (it.next())
            out.push_back(it.getItem());
        return out;
    }
};

struct Rig {
    TimelineDoc doc;
    TimelineViewState state;
    ClipSelectionModel selection;
    AppUndoManager undo;
    MenuLane lane{state, selection};
    ClipId audio, midi;
    std::vector<std::pair<ClipId, bool>> calls;

    Rig() {
        lane.setTimelineDoc(&doc);
        lane.setUndoManager(&undo);
        lane.setSize(1200, 400);
        const auto audioTrack = doc.addTrack(TrackKind::Audio, "Hum");
        const auto midiTrack = doc.addTrack(TrackKind::Midi, "Keys");
        audio = doc.addClip(audioTrack, 0.0, 8.0, "Hum take");
        midi = doc.addClip(midiTrack, 0.0, 4.0, "Notes");
        doc.setClipAsset(audio, "Audio/hum.wav", 0.0);
        lane.onConvertToMelodyRequested = [this](ClipId id, bool snap) { calls.emplace_back(id, snap); };
    }
};

const juce::PopupMenu::Item* find(const std::vector<juce::PopupMenu::Item>& items, const juce::String& text) {
    for (const auto& item : items)
        if (item.text == text)
            return &item;
    return nullptr;
}

} // namespace

TEST(ConvertToMelodyMenuTests, AudioClipOffersBothItemsAndEachCallsTheHost) {
    Rig rig;
    const auto items = rig.lane.itemsFor(rig.audio);
    const auto* sung = find(items, "Convert to melody (as sung)");
    const auto* snapped = find(items, "Convert to melody (snap to key and grid)");
    ASSERT_NE(sung, nullptr);
    ASSERT_NE(snapped, nullptr);
    ASSERT_TRUE(sung->action);
    sung->action();
    snapped->action();
    ASSERT_EQ(rig.calls.size(), 2u);
    EXPECT_EQ(rig.calls[0], std::make_pair(rig.audio, false));
    EXPECT_EQ(rig.calls[1], std::make_pair(rig.audio, true));
}

TEST(ConvertToMelodyMenuTests, MidiClipAndUnwiredHostGetNoItems) {
    Rig rig;
    EXPECT_EQ(find(rig.lane.itemsFor(rig.midi), "Convert to melody (as sung)"), nullptr);
    EXPECT_EQ(find(rig.lane.itemsFor(rig.midi), "Convert to melody (snap to key and grid)"), nullptr);
    rig.lane.onConvertToMelodyRequested = nullptr;
    EXPECT_EQ(find(rig.lane.itemsFor(rig.audio), "Convert to melody (as sung)"), nullptr);
}
