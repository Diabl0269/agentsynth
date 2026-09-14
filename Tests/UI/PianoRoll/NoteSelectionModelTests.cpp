// PianoRoll NoteSelectionModel tests: synth::ui::NoteSelectionModel — mirrors
// ClipSelectionModelTests.cpp's coverage, keyed on synth::NoteId.
// Shared PianoRollFixture and helpers live in PianoRollTestHelpers.h.

#include "PianoRollTestHelpers.h"

#include "UI/PianoRoll/NoteSelectionModel.h"

// ============================================================================
// 1. NoteSelectionModel
// ============================================================================

TEST(NoteSelectionModel, StartsEmpty) {
    NoteSelectionModel sel;
    EXPECT_TRUE(sel.isEmpty());
    EXPECT_EQ(sel.size(), 0);
    EXPECT_FALSE(sel.contains(nid(1)));
}

TEST(NoteSelectionModel, AddIsIdempotent) {
    NoteSelectionModel sel;
    EXPECT_TRUE(sel.add(nid(7)));
    EXPECT_FALSE(sel.add(nid(7))) << "adding an already-selected id must report no change";
    EXPECT_EQ(sel.size(), 1);
    EXPECT_TRUE(sel.contains(nid(7)));
}

TEST(NoteSelectionModel, RejectsInvalidNoteIdZero) {
    NoteSelectionModel sel;
    EXPECT_FALSE(sel.add(nid(0)));
    EXPECT_TRUE(sel.isEmpty());
}

TEST(NoteSelectionModel, RemoveReportsWhetherAnythingWasRemoved) {
    NoteSelectionModel sel;
    sel.add(nid(3));
    EXPECT_TRUE(sel.remove(nid(3)));
    EXPECT_FALSE(sel.remove(nid(3)));
    EXPECT_TRUE(sel.isEmpty());
}

TEST(NoteSelectionModel, ToggleReturnsStateAfterToggling) {
    NoteSelectionModel sel;
    EXPECT_TRUE(sel.toggle(nid(2))) << "toggling an unselected id selects it";
    EXPECT_TRUE(sel.contains(nid(2)));
    EXPECT_FALSE(sel.toggle(nid(2))) << "toggling a selected id deselects it";
    EXPECT_FALSE(sel.contains(nid(2)));
}

TEST(NoteSelectionModel, SetSelectionReplacesAndDeduplicates) {
    NoteSelectionModel sel;
    sel.add(nid(99));
    sel.setSelection({nid(1), nid(2), nid(2), nid(0)});

    EXPECT_EQ(sel.size(), 2) << "duplicates collapse and the invalid id is dropped";
    EXPECT_TRUE(sel.contains(nid(1)));
    EXPECT_TRUE(sel.contains(nid(2)));
    EXPECT_FALSE(sel.contains(nid(99))) << "setSelection replaces rather than merges";
}

TEST(NoteSelectionModel, GetSelectedIsOrderedByValueRegardlessOfInsertionOrder) {
    NoteSelectionModel a;
    a.setSelection({nid(30), nid(10), nid(20)});
    NoteSelectionModel b;
    b.setSelection({nid(10), nid(20), nid(30)});

    auto expected = std::vector<NoteId>{nid(10), nid(20), nid(30)};
    EXPECT_EQ(a.getSelected(), expected);
    EXPECT_EQ(b.getSelected(), expected);
}

TEST(NoteSelectionModel, ClearEmptiesEverything) {
    NoteSelectionModel sel;
    sel.setSelection({nid(1), nid(2)});
    sel.clear();
    EXPECT_TRUE(sel.isEmpty());
}

TEST(NoteSelectionModel, RetainOnlyDropsIdsThatNoLongerExist) {
    NoteSelectionModel sel;
    sel.setSelection({nid(1), nid(2), nid(3)});

    EXPECT_TRUE(sel.retainOnly({nid(1), nid(3)}));
    EXPECT_EQ(sel.size(), 2);
    EXPECT_TRUE(sel.contains(nid(1)));
    EXPECT_FALSE(sel.contains(nid(2)));
    EXPECT_TRUE(sel.contains(nid(3)));
}

TEST(NoteSelectionModel, RetainOnlyReportsNoChangeWhenEverythingSurvives) {
    NoteSelectionModel sel;
    sel.setSelection({nid(1), nid(2)});
    EXPECT_FALSE(sel.retainOnly({nid(1), nid(2), nid(5)}));
    EXPECT_EQ(sel.size(), 2);
}

TEST(NoteSelectionModel, RetainOnlyWithNothingAliveClearsSelection) {
    NoteSelectionModel sel;
    sel.setSelection({nid(1), nid(2)});
    EXPECT_TRUE(sel.retainOnly({}));
    EXPECT_TRUE(sel.isEmpty());
}
