// Moving and placing several section placements as one group: each member keeps its distance from the group's left
// edge, with its music, in make-room and cover, and the whole group as one undo step.

#include "AppUndoManager.h"
#include "TimelineDocSectionTestHelpers.h"

namespace {

// Four sections on one track, each holding one named clip: A over 0..8, B over 8..12, C over 12..20, D over 20..24.
struct FourSections {
    TrackId track;
    std::vector<PlacementId> ids; // A, B, C, D
};

FourSections buildFour(TimelineDoc& doc) {
    FourSections f;
    f.track = doc.addTrack(TrackKind::Midi, "T");
    const std::vector<std::pair<double, double>> spans{{0.0, 8.0}, {8.0, 12.0}, {12.0, 20.0}, {20.0, 24.0}};
    const char* names[] = {"A", "B", "C", "D"};
    for (std::size_t i = 0; i < spans.size(); ++i) {
        doc.addClip(f.track, spans[i].first, 2.0, names[i]);
        doc.makeSection(spans[i].first, spans[i].second, names[i], 0, kSpb);
    }
    for (const auto& placement : doc.getPlacements())
        f.ids.push_back(placement.id);
    return f;
}

double startOf(const TimelineDoc& doc, PlacementId id) { return doc.getPlacement(id)->startBeat; }

// The names of the clips on the track, in time order: where each section's music is.
std::vector<std::pair<double, juce::String>> clipsOf(const TimelineDoc& doc, TrackId track) {
    std::vector<std::pair<double, juce::String>> out;
    for (const auto& clip : doc.getTrack(track)->clips)
        out.emplace_back(clip.startBeat, clip.name);
    return out;
}

} // namespace

TEST_F(TimelineDocTest, MovePlacementsTogetherMakeRoomKeepsTheGapBetweenMembersAndTakesWhatIsInItAlong) {
    // Regression test for FRO1753: A and C landed back to back and B's place closed up between them.
    // Regression test for FRO1975: B closed up while the group's span opened again, so the song grew by B's bars.
    const auto f = buildFour(doc);
    // A and C after D, in the layout with their span (0..20) taken out: D 0..4, then A at 4, B at 12, C at 16.
    ASSERT_TRUE(doc.movePlacementsTogether({f.ids[2], f.ids[0]}, 4.0, SectionDropMode::MakeRoom, kSpb));
    EXPECT_DOUBLE_EQ(startOf(doc, f.ids[3]), 0.0);
    EXPECT_DOUBLE_EQ(startOf(doc, f.ids[0]), 4.0);
    EXPECT_DOUBLE_EQ(startOf(doc, f.ids[1]), 12.0) << "B rides between them";
    EXPECT_DOUBLE_EQ(startOf(doc, f.ids[2]), 16.0);
    using Row = std::pair<double, juce::String>;
    EXPECT_EQ(clipsOf(doc, f.track), (std::vector<Row>{{0.0, "D"}, {4.0, "A"}, {12.0, "B"}, {16.0, "C"}}))
        << "every section's music moved with it";
}

TEST_F(TimelineDocTest, MovePlacementsTogetherMakeRoomAwayAndBackRestoresTheSong) {
    // Regression test for FRO1975: moving A and C (B between) right and back left the rest of the song pushed later.
    const auto f = buildFour(doc);
    const auto before = clipsOf(doc, f.track);
    ASSERT_TRUE(doc.movePlacementsTogether({f.ids[0], f.ids[2]}, 4.0, SectionDropMode::MakeRoom, kSpb));
    ASSERT_TRUE(doc.movePlacementsTogether({f.ids[0], f.ids[2]}, 0.0, SectionDropMode::MakeRoom, kSpb));
    const std::vector<double> starts{0.0, 8.0, 12.0, 20.0};
    for (std::size_t i = 0; i < f.ids.size(); ++i)
        EXPECT_DOUBLE_EQ(startOf(doc, f.ids[i]), starts[i]) << "section " << i;
    EXPECT_EQ(clipsOf(doc, f.track), before);
    ASSERT_TRUE(doc.movePlacementsTogether({f.ids[0], f.ids[2]}, 0.0, SectionDropMode::MakeRoom, kSpb))
        << "a drop at the group's own start is accepted";
    EXPECT_EQ(clipsOf(doc, f.track), before) << "...and changes nothing";
}

TEST_F(TimelineDocTest, MovePlacementsTogetherKeepsLanePointsAndMarkersInMembersAndInTheGap) {
    const auto f = buildFour(doc);
    const auto lane = doc.addLane(f.track, "uuid", "cut", makeRange(0.0f, 1000.0f, 0.0f));
    for (const double beat : {1.0, 9.0, 13.0, 21.0})
        doc.addBreakpoint(lane, beat, 100.0 + beat);
    doc.addMarker(1.0, "inA", 0);
    doc.addMarker(9.0, "inB", 0);
    doc.addMarker(13.0, "inC", 0);
    ASSERT_TRUE(doc.movePlacementsTogether({f.ids[0], f.ids[2]}, 4.0, SectionDropMode::MakeRoom, kSpb));
    // The span 0..20 goes, D closes up to 0..4; A lands at 4, B (not a member) rides to 12 and C to 16.
    EXPECT_EQ(pointBeats(doc, lane), (std::vector<double>{1, 5, 13, 17}));
    std::map<juce::String, double> marks;
    for (const auto& marker : doc.getMarkers())
        marks[marker.text] = marker.beat;
    EXPECT_DOUBLE_EQ(marks["inB"], 13.0) << "a marker between the members rides along";
    EXPECT_EQ(marks.count("inA"), 0u) << "a marker in a member's bars goes with them, as in a single move";
    EXPECT_EQ(marks.count("inC"), 0u);
}

TEST_F(TimelineDocTest, MovePlacementsTogetherCoverClearsTheirBarsAndCoversWhatEachMemberLandsOn) {
    const auto f = buildFour(doc);
    // B and D keep their 12-beat distance over A: B lands on 0..4 (covers A) and D on 12..16 (covers C); the old bars
    // of B and D are left empty.
    ASSERT_TRUE(doc.movePlacementsTogether({f.ids[1], f.ids[3]}, 0.0, SectionDropMode::Cover, kSpb));
    EXPECT_EQ(doc.getPlacement(f.ids[0]), nullptr);
    EXPECT_EQ(doc.getPlacement(f.ids[2]), nullptr);
    EXPECT_DOUBLE_EQ(startOf(doc, f.ids[1]), 0.0);
    EXPECT_DOUBLE_EQ(startOf(doc, f.ids[3]), 12.0);
    using Row = std::pair<double, juce::String>;
    EXPECT_EQ(clipsOf(doc, f.track), (std::vector<Row>{{0.0, "B"}, {12.0, "D"}}));
}

TEST_F(TimelineDocTest, MovePlacementsTogetherCoverLeavesWhatSitsBetweenLandedMembers) {
    const auto f = buildFour(doc);
    doc.addClip(f.track, 26.0, 2.0, "G"); // loose music past D, where the gap between the landed members will be
    // A and D (24-beat span) at 12: A lands on 12..20 (covers C), D on 32..36; G at 26 is between them.
    ASSERT_TRUE(doc.movePlacementsTogether({f.ids[0], f.ids[3]}, 12.0, SectionDropMode::Cover, kSpb));
    EXPECT_EQ(doc.getPlacement(f.ids[2]), nullptr);
    EXPECT_NE(doc.getPlacement(f.ids[1]), nullptr);
    EXPECT_DOUBLE_EQ(startOf(doc, f.ids[0]), 12.0);
    EXPECT_DOUBLE_EQ(startOf(doc, f.ids[3]), 32.0);
    using Row = std::pair<double, juce::String>;
    EXPECT_EQ(clipsOf(doc, f.track), (std::vector<Row>{{8.0, "B"}, {12.0, "A"}, {26.0, "G"}, {32.0, "D"}}));
}

TEST_F(TimelineDocTest, MovePlacementsTogetherWithOneIdIsMovePlacementAndAnUnknownIdChangesNothing) {
    const auto f = buildFour(doc);
    ASSERT_TRUE(doc.movePlacementsTogether({f.ids[0]}, 16.0, SectionDropMode::MakeRoom, kSpb));
    EXPECT_DOUBLE_EQ(startOf(doc, f.ids[0]), 16.0);
    const auto revision = doc.getRevision();
    EXPECT_FALSE(doc.movePlacementsTogether({f.ids[1], PlacementId{999}}, 0.0, SectionDropMode::MakeRoom, kSpb));
    EXPECT_EQ(doc.getRevision(), revision);
}

TEST_F(TimelineDocTest, MovingAGroupIsOneUndoStepThatRestoresEveryMember) {
    const auto f = buildFour(doc);
    const auto before = clipsOf(doc, f.track);
    AppUndoManager undo;
    undo.recordTimelineChange(
        doc, [&] { doc.movePlacementsTogether({f.ids[0], f.ids[1]}, 12.0, SectionDropMode::MakeRoom, kSpb); });
    ASSERT_DOUBLE_EQ(startOf(doc, f.ids[0]), 12.0);
    ASSERT_TRUE(undo.undo());
    for (std::size_t i = 0; i < f.ids.size(); ++i)
        ASSERT_NE(doc.getPlacement(f.ids[i]), nullptr);
    EXPECT_DOUBLE_EQ(startOf(doc, f.ids[0]), 0.0);
    EXPECT_DOUBLE_EQ(startOf(doc, f.ids[1]), 8.0);
    EXPECT_DOUBLE_EQ(startOf(doc, f.ids[2]), 12.0);
    EXPECT_EQ(clipsOf(doc, f.track), before);
    EXPECT_FALSE(undo.undo()) << "the group move was one step";
}

TEST_F(TimelineDocTest, MovingAGappedGroupIsOneUndoStepThatRestoresEveryMember) {
    const auto f = buildFour(doc);
    const auto before = clipsOf(doc, f.track);
    AppUndoManager undo;
    undo.recordTimelineChange(
        doc, [&] { doc.movePlacementsTogether({f.ids[0], f.ids[2]}, 4.0, SectionDropMode::MakeRoom, kSpb); });
    ASSERT_DOUBLE_EQ(startOf(doc, f.ids[2]), 16.0);
    ASSERT_TRUE(undo.undo());
    EXPECT_DOUBLE_EQ(startOf(doc, f.ids[0]), 0.0);
    EXPECT_DOUBLE_EQ(startOf(doc, f.ids[1]), 8.0);
    EXPECT_DOUBLE_EQ(startOf(doc, f.ids[2]), 12.0);
    EXPECT_DOUBLE_EQ(startOf(doc, f.ids[3]), 20.0);
    EXPECT_EQ(clipsOf(doc, f.track), before);
    EXPECT_FALSE(undo.undo());
}

TEST_F(TimelineDocTest, PlaceSectionsTogetherKeepsTheGapBetweenTheCopies) {
    const auto f = buildFour(doc);
    // Copies of A and C after D: C's copy lands 12 after A's, where B sat between the originals.
    const auto placed = doc.placeSectionsTogether({f.ids[0], f.ids[2]}, 24.0, SectionDropMode::MakeRoom, kSpb);
    ASSERT_EQ(placed.size(), 2u);
    EXPECT_DOUBLE_EQ(startOf(doc, placed[0]), 24.0);
    EXPECT_DOUBLE_EQ(startOf(doc, placed[1]), 36.0);
    using Row = std::pair<double, juce::String>;
    const auto clips = clipsOf(doc, f.track);
    EXPECT_EQ(std::vector<Row>(clips.end() - 2, clips.end()), (std::vector<Row>{{24.0, "A"}, {36.0, "C"}}));
}

TEST_F(TimelineDocTest, PlaceSectionsTogetherMakeRoomOpensTheSpanOnceAndCoverClearsEachCopysPlace) {
    const auto f = buildFour(doc);
    // Make room: a gapped pair (A, C; span 20) copied at 8 pushes B and D by the whole span.
    const auto placed = doc.placeSectionsTogether({f.ids[0], f.ids[2]}, 8.0, SectionDropMode::MakeRoom, kSpb);
    ASSERT_EQ(placed.size(), 2u);
    EXPECT_DOUBLE_EQ(startOf(doc, f.ids[1]), 28.0);
    EXPECT_DOUBLE_EQ(startOf(doc, f.ids[3]), 40.0);
    EXPECT_DOUBLE_EQ(startOf(doc, placed[1]), 20.0);
    // Cover: the pair copied at 24 in a fresh song lands on 24..32 and 36..44; loose music at 33 between them stays.
    TimelineDoc other;
    const auto g = buildFour(other);
    other.addClip(g.track, 33.0, 2.0, "G");
    const auto copies = other.placeSectionsTogether({g.ids[0], g.ids[2]}, 24.0, SectionDropMode::Cover, kSpb);
    ASSERT_EQ(copies.size(), 2u);
    EXPECT_DOUBLE_EQ(startOf(other, copies[1]), 36.0);
    using Row = std::pair<double, juce::String>;
    const auto clips = clipsOf(other, g.track);
    EXPECT_NE(std::find(clips.begin(), clips.end(), Row{33.0, "G"}), clips.end()) << "the gap's music is left alone";
}

TEST_F(TimelineDocTest, PlaceSectionsTogetherPlacesEachSectionAgainBackToBack) {
    const auto f = buildFour(doc);
    const auto placed = doc.placeSectionsTogether({f.ids[1], f.ids[0]}, 24.0, SectionDropMode::MakeRoom, kSpb);
    ASSERT_EQ(placed.size(), 2u);
    EXPECT_DOUBLE_EQ(startOf(doc, placed[0]), 24.0);
    EXPECT_DOUBLE_EQ(startOf(doc, placed[1]), 32.0);
    EXPECT_EQ(doc.getPlacement(placed[0])->section, doc.getPlacement(f.ids[0])->section) << "time order: A first";
    EXPECT_EQ(doc.getPlacement(placed[1])->section, doc.getPlacement(f.ids[1])->section);
    EXPECT_EQ(clipsOf(doc, f.track).back(), (std::pair<double, juce::String>{32.0, "B"}));
}

TEST_F(TimelineDocTest, PlaceSectionsTogetherCoveringAnOriginalStillPlacesItsMusic) {
    const auto f = buildFour(doc);
    // Copies of A and B over 4..16: the span covers the original A, B (the only placement of its section) and C.
    const auto placed = doc.placeSectionsTogether({f.ids[0], f.ids[1]}, 4.0, SectionDropMode::Cover, kSpb);
    ASSERT_EQ(placed.size(), 2u);
    EXPECT_DOUBLE_EQ(startOf(doc, placed[0]), 4.0);
    EXPECT_DOUBLE_EQ(startOf(doc, placed[1]), 12.0);
    EXPECT_EQ(doc.getPlacement(f.ids[0]), nullptr);
    EXPECT_EQ(doc.getPlacement(f.ids[1]), nullptr);
    EXPECT_EQ(doc.getPlacement(f.ids[2]), nullptr);
    ASSERT_NE(doc.getSection(doc.getPlacement(placed[1])->section), nullptr) << "B lives on in its copy";
    using Row = std::pair<double, juce::String>;
    EXPECT_EQ(clipsOf(doc, f.track), (std::vector<Row>{{0.0, "A"}, {4.0, "A"}, {12.0, "B"}, {20.0, "D"}}));
}
