// A Drum Kit track's clips draw the kit's shown pads as the rows of their note preview: a pad switched off has no row
// (its hits still play), and the rows below it close up.

#include "Modules/DrumKit/DrumKitModule.h"
#include "TimelineClipLane/TimelineClipLaneTestFixture.h"
#include "UI/DrumMachine/DrumLanes.h"

namespace {

constexpr int kCymbal = 49;
constexpr int kKick = 36;

struct KitClipFixture {
    ClipLaneFixture f;
    DrumKitModule kit;
    synth::ClipId clip;

    KitClipFixture() {
        const auto track = f.doc.addTrack(TrackKind::Midi, "Drums");
        clip = f.doc.addClip(track, 0.0, 8.0, "");
        f.doc.addNote(clip, makeNote(2.0, kKick, 1.0));
        f.doc.addNote(clip, makeNote(4.0, kCymbal, 1.0));
        f.lane.kitRowsProvider = [this](synth::TrackId, synth::ui::KitRows& rows) {
            rows = synth::ui::kitRowsFor(kit);
            return true;
        };
        f.lane.refreshFromDoc();
    }
    void hide(int pad) {
        kit.setPadShown(pad, false);
        f.lane.refreshKitRows();
    }
    // The y of every drawn note line, read down one column inside the clip where a note starts at beat 4.
    std::vector<int> noteLineYs() {
        const auto image = f.lane.createComponentSnapshot(f.lane.getLocalBounds());
        const auto rect = f.lane.getClipRect(clip);
        const int x = (int)f.state.beatToX(4.5);
        const auto fill = image.getPixelAt(rect.getRight() - 4, rect.getCentreY());
        std::vector<int> ys;
        for (int y = rect.getY() + 1; y < rect.getBottom() - 1; ++y) {
            const auto px = image.getPixelAt(x, y);
            if (std::abs(px.getRed() - fill.getRed()) + std::abs(px.getGreen() - fill.getGreen()) > 40)
                ys.push_back(y);
        }
        return ys;
    }
};

} // namespace

// A pad's hit keeps its row while the pad is shown, and loses it when it is switched off.
TEST(TimelineClipKitRows, AHiddenPadsHitsAreNotDrawnInTheClip) {
    KitClipFixture k;
    const auto before = k.noteLineYs();
    ASSERT_FALSE(before.empty()) << "the cymbal hit is drawn while its pad is shown";
    k.hide(6); // the cymbal
    EXPECT_TRUE(k.noteLineYs().empty()) << "no row for the hidden cymbal";
    ASSERT_EQ(k.f.doc.getClip(k.clip)->notes.size(), 2u) << "the hit is still in the clip, so it still plays";
}

TEST(TimelineClipKitRows, TheRowsCloseUpWhenAPadAboveIsHidden) {
    KitClipFixture k;
    const auto before = k.noteLineYs();
    ASSERT_FALSE(before.empty());
    k.hide(0); // the kick: every row after it moves up
    const auto after = k.noteLineYs();
    ASSERT_FALSE(after.empty());
    EXPECT_LT(after.front(), before.front()) << "the cymbal's line rose into the freed row";
    k.kit.showAllPads();
    k.f.lane.refreshKitRows();
    EXPECT_EQ(k.noteLineYs(), before) << "Show all puts the rows back";
}

TEST(TimelineClipKitRows, AClipOnATrackWithoutAKitKeepsThePitchScale) {
    KitClipFixture k;
    k.f.lane.kitRowsProvider = [](synth::TrackId, synth::ui::KitRows&) { return false; };
    k.f.lane.refreshKitRows();
    EXPECT_FALSE(k.noteLineYs().empty());
    k.kit.setPadShown(6, false);
    k.f.lane.refreshKitRows();
    EXPECT_FALSE(k.noteLineYs().empty()) << "a piano-roll track is not affected by any kit";
}
