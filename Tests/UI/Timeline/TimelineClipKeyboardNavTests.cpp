// TimelineClipKeyboardNavTests.cpp
//
// The pure pieces of clip keyboard mode: clip-to-clip navigation over a TimelineDoc and the
// screen-reader description of a clip.

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Timeline/ClipAccessibilityText.h"
#include "UI/Timeline/ClipKeyboardNav.h"
#include <gtest/gtest.h>

using synth::ClipId;
using synth::TimelineDoc;
using synth::TrackKind;
namespace nav = synth::ui::clipnav;

TEST(ClipKeyboardNavTest, FirstClipFromPicksAtOrAfterTheBeatElseTheFirst) {
    TimelineDoc doc;
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto a = doc.addClip(track, 4.0, 2.0, "a");
    const auto b = doc.addClip(track, 10.0, 2.0, "b");
    const auto& t = *doc.getTrack(track);

    EXPECT_EQ(nav::firstClipFrom(t, 0.0), a);
    EXPECT_EQ(nav::firstClipFrom(t, 4.0), a);
    EXPECT_EQ(nav::firstClipFrom(t, 4.5), b);
    EXPECT_EQ(nav::firstClipFrom(t, 11.0), a) << "past every clip: the track's first";

    const auto emptyTrack = doc.addTrack(TrackKind::Midi, "E");
    EXPECT_FALSE(nav::firstClipFrom(*doc.getTrack(emptyTrack), 0.0).isValid());
}

TEST(ClipKeyboardNavTest, AdjacentOnTrackFollowsStartOrderAndStopsAtTheEnds) {
    TimelineDoc doc;
    const auto track = doc.addTrack(TrackKind::Midi, "T");
    const auto late = doc.addClip(track, 10.0, 2.0, "late");
    const auto early = doc.addClip(track, 1.0, 2.0, "early");
    const auto& t = *doc.getTrack(track);

    EXPECT_EQ(nav::adjacentOnTrack(t, early, 1), late);
    EXPECT_EQ(nav::adjacentOnTrack(t, late, -1), early);
    EXPECT_FALSE(nav::adjacentOnTrack(t, early, -1).isValid());
    EXPECT_FALSE(nav::adjacentOnTrack(t, late, 1).isValid());
    EXPECT_FALSE(nav::adjacentOnTrack(t, ClipId{9999}, 1).isValid());
}

TEST(ClipKeyboardNavTest, NearestOnAdjacentTrackSkipsEmptyTracksAndBreaksTiesEarlier) {
    TimelineDoc doc;
    const auto top = doc.addTrack(TrackKind::Midi, "top");
    const auto gap = doc.addTrack(TrackKind::Midi, "gap");
    const auto bottom = doc.addTrack(TrackKind::Midi, "bottom");
    const auto src = doc.addClip(top, 8.0, 2.0, "src");
    const auto before = doc.addClip(bottom, 6.0, 2.0, "before");
    doc.addClip(bottom, 10.0, 2.0, "after");
    doc.addClip(bottom, 30.0, 2.0, "far");
    (void)gap;

    EXPECT_EQ(nav::nearestOnAdjacentTrack(doc, src, 1), before) << "6 and 10 are both 2 away: the earlier wins";
    EXPECT_FALSE(nav::nearestOnAdjacentTrack(doc, src, -1).isValid());
    EXPECT_FALSE(nav::nearestOnAdjacentTrack(doc, ClipId{9999}, 1).isValid());
}

TEST(ClipAccessibilityTextTest, DescribesAWholeBarMidiClip) {
    synth::Clip clip;
    clip.name = "Bassline";
    clip.startBeat = 16.0;
    clip.lengthBeats = 16.0;
    synth::Track track;
    track.name = "Bass";
    EXPECT_EQ(synth::ui::describeClipForAccessibility(clip, track, 4.0), "MIDI clip Bassline, track Bass, bars 5 to 9");
}

TEST(ClipAccessibilityTextTest, UsesTheTimeSignatureAndNamesAudioAndMutedClips) {
    synth::Clip clip;
    clip.name = "Vox";
    clip.assetRef = "Audio/vox.wav";
    clip.startBeat = 6.0;
    clip.lengthBeats = 6.0;
    clip.muted = true;
    synth::Track track;
    track.name = "Vocals";
    EXPECT_EQ(synth::ui::describeClipForAccessibility(clip, track, 3.0),
              "Audio clip Vox, track Vocals, bars 3 to 5, muted");
}

TEST(ClipAccessibilityTextTest, SpellsOutClipsThatDoNotStartOrEndOnABarLine) {
    synth::Clip clip;
    clip.name = "Fill";
    clip.startBeat = 6.5;
    clip.lengthBeats = 2.0;
    synth::Track track;
    track.name = "Drums";
    EXPECT_EQ(synth::ui::describeClipForAccessibility(clip, track, 4.0),
              "MIDI clip Fill, track Drums, from bar 2 beat 3.5 to bar 3 beat 1.5");
    EXPECT_EQ(synth::ui::describeBarPosition(0.0, 4.0), "1");
}
