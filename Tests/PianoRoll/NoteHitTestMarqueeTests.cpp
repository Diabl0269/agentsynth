// PianoRoll noteHitTestMarquee tests (named NoteSelectionMarqueeTest so it stays under the
// "NoteSelection*" gtest filter, alongside NoteSelectionModelTests.cpp).
// Shared PianoRollFixture and helpers live in PianoRollTestHelpers.h.

#include "PianoRollTestHelpers.h"

#include "../../Source/UI/NoteSelectionModel.h"

// ============================================================================
// 2. noteHitTestMarquee (named NoteSelectionMarqueeTest so it stays under the "NoteSelection*"
//    gtest filter, alongside the model tests above)
// ============================================================================

namespace {
std::vector<std::pair<NoteId, juce::Rectangle<int>>> threeNoteRects() {
    return {
        {nid(1), juce::Rectangle<int>(0, 0, 40, 10)},
        {nid(2), juce::Rectangle<int>(100, 0, 40, 10)},
        {nid(3), juce::Rectangle<int>(200, 200, 40, 10)},
    };
}
} // namespace

TEST(NoteSelectionMarqueeTest, SelectsFullyEnclosedNotes) {
    auto hits = synth::ui::noteHitTestMarquee(juce::Rectangle<int>(-10, -10, 160, 30), threeNoteRects());
    ASSERT_EQ(hits.size(), 2u);
    EXPECT_EQ(hits[0], nid(1));
    EXPECT_EQ(hits[1], nid(2));
}

TEST(NoteSelectionMarqueeTest, SelectsPartiallyTouchedNotes) {
    auto hits = synth::ui::noteHitTestMarquee(juce::Rectangle<int>(30, 5, 20, 20), threeNoteRects());
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0], nid(1));
}

TEST(NoteSelectionMarqueeTest, MissesNotesOutsideTheBand) {
    auto hits = synth::ui::noteHitTestMarquee(juce::Rectangle<int>(50, 50, 20, 20), threeNoteRects());
    EXPECT_TRUE(hits.empty());
}

TEST(NoteSelectionMarqueeTest, DegenerateMarqueeSelectsNothing) {
    auto hits = synth::ui::noteHitTestMarquee(juce::Rectangle<int>(10, 5, 0, 0), threeNoteRects());
    EXPECT_TRUE(hits.empty());
}

TEST(NoteSelectionMarqueeTest, IgnoresInvalidNoteIds) {
    std::vector<std::pair<NoteId, juce::Rectangle<int>>> rects{{nid(0), juce::Rectangle<int>(0, 0, 100, 100)}};
    auto hits = synth::ui::noteHitTestMarquee(juce::Rectangle<int>(0, 0, 200, 200), rects);
    EXPECT_TRUE(hits.empty());
}

TEST(NoteSelectionMarqueeTest, EmptyListYieldsNoHits) {
    auto hits = synth::ui::noteHitTestMarquee(juce::Rectangle<int>(0, 0, 500, 500), {});
    EXPECT_TRUE(hits.empty());
}
