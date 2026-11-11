// Moving and placing several section placements as one group: back to back from the landing with their music, in
// make-room and cover, and the whole group as one undo step.

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

TEST_F(TimelineDocTest, MovePlacementsTogetherMakeRoomLandsBackToBackWithTheirMusic) {
    const auto f = buildFour(doc);
    // A and C after D, in the layout with their bars closed up: B 0..4, D 4..8, then A at 8 and C at 16.
    ASSERT_TRUE(doc.movePlacementsTogether({f.ids[2], f.ids[0]}, 8.0, SectionDropMode::MakeRoom, kSpb));
    EXPECT_DOUBLE_EQ(startOf(doc, f.ids[1]), 0.0);
    EXPECT_DOUBLE_EQ(startOf(doc, f.ids[3]), 4.0);
    EXPECT_DOUBLE_EQ(startOf(doc, f.ids[0]), 8.0);
    EXPECT_DOUBLE_EQ(startOf(doc, f.ids[2]), 16.0);
    using Row = std::pair<double, juce::String>;
    EXPECT_EQ(clipsOf(doc, f.track), (std::vector<Row>{{0.0, "B"}, {4.0, "D"}, {8.0, "A"}, {16.0, "C"}}))
        << "each member's music moved with it";
}

TEST_F(TimelineDocTest, MovePlacementsTogetherCoverClearsTheirBarsAndCoversTheLanding) {
    const auto f = buildFour(doc);
    // B and D (8 beats) over A: A is covered, B's and D's old bars are left empty, C stays.
    ASSERT_TRUE(doc.movePlacementsTogether({f.ids[1], f.ids[3]}, 0.0, SectionDropMode::Cover, kSpb));
    EXPECT_EQ(doc.getPlacement(f.ids[0]), nullptr);
    EXPECT_DOUBLE_EQ(startOf(doc, f.ids[1]), 0.0);
    EXPECT_DOUBLE_EQ(startOf(doc, f.ids[3]), 4.0);
    EXPECT_DOUBLE_EQ(startOf(doc, f.ids[2]), 12.0);
    using Row = std::pair<double, juce::String>;
    EXPECT_EQ(clipsOf(doc, f.track), (std::vector<Row>{{0.0, "B"}, {4.0, "D"}, {12.0, "C"}}));
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
