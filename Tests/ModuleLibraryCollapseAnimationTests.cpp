// ModuleLibraryCollapseAnimationTests.cpp
// Tests for the animated collapse/expand of library sections.
//
//   • snap-when-hidden — no VBlank off screen, so the layout lands on its final state at once
//   • band geometry    — a folding section shrinks continuously and pushes later rows up
//   • truncation       — rows are cut at the band edge, never squashed, and stop hit-testing
//   • endpoints        — progress 0 and 1 reproduce the un-animated layout exactly
//
// The animation itself is VBlank-driven and cannot tick headlessly, so these drive
// setSectionProgress() directly — the same value the animator writes each frame.

#include "../Source/UI/ModuleLibraryComponent.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using RowKind = ModuleLibraryComponent::RowKind;

namespace {

constexpr int kPanelWidth = 200;
constexpr int kPanelHeight = 4000; // tall enough that nothing scrolls, isolating fold geometry

/** A section with several rows, so a half-fold is unambiguous. */
juce::String sectionWithManyRows(const ModuleLibraryComponent& comp) { return "Sources"; }

int visibleRowsInSection(const ModuleLibraryComponent& comp, const juce::String& section) {
    int count = 0;
    for (const auto& row : comp.buildRows()) {
        const auto& entry = comp.getEntry(row.entryIndex);
        if (entry.kind != RowKind::Header && entry.section == section)
            ++count;
    }
    return count;
}

/** Y of a header row, or -1. */
int headerY(const ModuleLibraryComponent& comp, const juce::String& header) {
    for (const auto& row : comp.buildRows()) {
        const auto& entry = comp.getEntry(row.entryIndex);
        if (entry.kind == RowKind::Header && entry.text == header)
            return row.y;
    }
    return -1;
}

} // namespace

// ============================================================================
// Off screen there is no VBlank, so collapsing must not leave the view stuck
// ============================================================================

TEST(ModuleLibraryCollapseAnimation, CollapsingWhileHiddenSnapsInsteadOfHanging) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kPanelHeight);

    comp.setSectionCollapsed("Sources", true);

    EXPECT_FALSE(comp.isCollapseAnimating()) << "a component that is not showing must not start an animation";
    EXPECT_FLOAT_EQ(comp.getSectionProgress("Sources"), 1.0f) << "the fold must land on its final value immediately";
    EXPECT_EQ(visibleRowsInSection(comp, "Sources"), 0);
}

TEST(ModuleLibraryCollapseAnimation, RestoringPersistedStateSnaps) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kPanelHeight);

    // Launch-time restore must not look like the sidebar folding itself up.
    comp.setCollapsedSections({"Sources", "Filters"});

    EXPECT_FALSE(comp.isCollapseAnimating());
    EXPECT_FLOAT_EQ(comp.getSectionProgress("Sources"), 1.0f);
    EXPECT_FLOAT_EQ(comp.getSectionProgress("Filters"), 1.0f);
}

TEST(ModuleLibraryCollapseAnimation, FinishCollapseAnimationLandsOnTheLogicalState) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kPanelHeight);

    comp.setSectionCollapsed("Sources", true);
    comp.setSectionProgress("Sources", 0.4f); // pretend a frame landed mid-flight
    ASSERT_FLOAT_EQ(comp.getSectionProgress("Sources"), 0.4f);

    comp.finishCollapseAnimation();
    EXPECT_FLOAT_EQ(comp.getSectionProgress("Sources"), 1.0f) << "finishing must agree with isSectionCollapsed()";
}

// ============================================================================
// Endpoints — the animation must not change what fully open / fully shut look like
// ============================================================================

TEST(ModuleLibraryCollapseAnimation, ProgressOneMatchesTheCollapsedLayout) {
    ModuleLibraryComponent open;
    open.setSize(kPanelWidth, kPanelHeight);
    open.setSectionCollapsed("Sources", true); // snaps to 1 headlessly
    const int collapsedHeight = open.getTotalContentHeight();
    const int collapsedRows = (int)open.buildRows().size();

    ModuleLibraryComponent scrubbed;
    scrubbed.setSize(kPanelWidth, kPanelHeight);
    scrubbed.setSectionProgress("Sources", 1.0f); // same fold, logical state untouched

    EXPECT_EQ(scrubbed.getTotalContentHeight(), collapsedHeight);
    EXPECT_EQ((int)scrubbed.buildRows().size(), collapsedRows);
}

TEST(ModuleLibraryCollapseAnimation, ProgressZeroLeavesEverySectionFullHeight) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kPanelHeight);

    for (const auto& row : comp.buildRows())
        if (comp.getEntry(row.entryIndex).kind != RowKind::Header)
            EXPECT_EQ(row.height, ModuleLibraryComponent::kItemHeight) << "an open section must not truncate rows";
}

// ============================================================================
// Band geometry — the part that only exists because of the animation
// ============================================================================

TEST(ModuleLibraryCollapseAnimation, HalfFoldedSectionShowsAboutHalfItsRows) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kPanelHeight);

    const auto section = sectionWithManyRows(comp);
    const int openRows = visibleRowsInSection(comp, section);
    ASSERT_GE(openRows, 4) << "test premise: the section needs enough rows for a half-fold to be visible";

    comp.setSectionProgress(section, 0.5f);
    const int halfRows = visibleRowsInSection(comp, section);

    EXPECT_GT(halfRows, 0) << "a half-folded section is still partly on screen";
    EXPECT_LT(halfRows, openRows) << "a half-folded section shows fewer rows than an open one";
}

TEST(ModuleLibraryCollapseAnimation, FoldingPushesLaterSectionsUpProportionally) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kPanelHeight);

    const auto section = sectionWithManyRows(comp);
    const int openY = headerY(comp, "Sequencing"); // the header immediately after "Sources"
    ASSERT_GT(openY, 0);

    comp.setSectionProgress(section, 1.0f);
    const int shutY = headerY(comp, "Sequencing");
    const int fullShift = openY - shutY;
    ASSERT_GT(fullShift, 0) << "closing a section must pull the next header up";

    comp.setSectionProgress(section, 0.5f);
    const int halfY = headerY(comp, "Sequencing");

    // Half folded should sit about halfway between the two extremes — the whole point of the tween.
    EXPECT_NEAR(halfY, openY - fullShift / 2, ModuleLibraryComponent::kItemHeight / 2);
    EXPECT_LT(halfY, openY);
    EXPECT_GT(halfY, shutY);
}

TEST(ModuleLibraryCollapseAnimation, ContentHeightShrinksMonotonicallyAsTheFoldProgresses) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kPanelHeight);

    const auto section = sectionWithManyRows(comp);
    int previous = comp.getTotalContentHeight() + 1;
    for (float p : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        comp.setSectionProgress(section, p);
        const int height = comp.getTotalContentHeight();
        EXPECT_LT(height, previous) << "content height must fall as the fold advances (p=" << p << ")";
        previous = height;
    }
}

TEST(ModuleLibraryCollapseAnimation, TheRowStraddlingTheBandEdgeIsTruncatedNotSquashed) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kPanelHeight);

    const auto section = sectionWithManyRows(comp);
    comp.setSectionProgress(section, 0.55f); // deliberately not a whole number of rows

    int truncated = 0;
    for (const auto& row : comp.buildRows()) {
        const auto& entry = comp.getEntry(row.entryIndex);
        if (entry.kind == RowKind::Header || entry.section != section)
            continue;
        EXPECT_GT(row.height, 0) << "a zero-height row must be dropped, not emitted";
        EXPECT_LE(row.height, ModuleLibraryComponent::kItemHeight);
        if (row.height < ModuleLibraryComponent::kItemHeight)
            ++truncated;
    }
    EXPECT_LE(truncated, 1) << "at most the one row crossing the band edge is clipped";
}

TEST(ModuleLibraryCollapseAnimation, RowsFoldedPastTheBandStopHitTesting) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, kPanelHeight);

    const auto section = sectionWithManyRows(comp);

    // Remember where the section's last row sits while open.
    int lastRowCentre = -1, lastEntry = -1;
    for (const auto& row : comp.buildRows()) {
        const auto& entry = comp.getEntry(row.entryIndex);
        if (entry.kind != RowKind::Header && entry.section == section) {
            lastRowCentre = row.y + row.height / 2;
            lastEntry = row.entryIndex;
        }
    }
    ASSERT_GE(lastEntry, 0);
    ASSERT_EQ(comp.getEntryIndexAt(lastRowCentre), lastEntry);

    comp.setSectionProgress(section, 0.5f);
    EXPECT_NE(comp.getEntryIndexAt(lastRowCentre), lastEntry)
        << "a row folded past the band edge must not answer hit-tests at its old position";
}

// ============================================================================
// Paint — mid-fold drawing takes a clipping path the endpoints never hit
// ============================================================================

TEST(ModuleLibraryCollapseAnimation, PaintingMidFoldDoesNotCrash) {
    ModuleLibraryComponent comp;
    comp.setSize(kPanelWidth, 900);

    for (float p : {0.15f, 0.5f, 0.85f}) {
        comp.setSectionProgress("Sources", p);
        comp.setSectionProgress("Snippets", 1.0f - p);

        juce::Image img(juce::Image::ARGB, kPanelWidth, 900, true);
        juce::Graphics g(img);
        EXPECT_NO_THROW(comp.paint(g)) << "paint must survive a partial fold (p=" << p << ")";
    }
}
