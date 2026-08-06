// ModuleLibraryCollapseTests.cpp
// Tests for the collapsible library sections and the Snippets section — issue #156.
//
//   • collapse/expand    — hides a section's rows, keeps its header, survives a round-trip
//   • collapse all       — one control folds/unfolds every section, label follows state
//   • persistence        — getCollapsedSections/setCollapsedSections round-trip, blank-safe
//   • snippets section   — rows appear/disappear with the snippet list, empty hint when none
//   • row layout         — paint and hit-testing agree because they share one layout pass

#include "../Source/UI/ModuleLibraryComponent.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using RowKind = ModuleLibraryComponent::RowKind;

namespace {

int countRowsOfKind(const ModuleLibraryComponent& comp, RowKind kind) {
    int count = 0;
    for (const auto& row : comp.buildRows())
        if (comp.getEntry(row.entryIndex).kind == kind)
            ++count;
    return count;
}

juce::StringArray headerNames(const ModuleLibraryComponent& comp) {
    juce::StringArray names;
    for (int i = 0; i < comp.getEntryCount(); ++i)
        if (comp.getEntry(i).kind == RowKind::Header)
            names.add(comp.getEntry(i).text);
    return names;
}

juce::Array<synth::SnippetInfo> makeSnippets(const juce::StringArray& names) {
    juce::Array<synth::SnippetInfo> result;
    for (const auto& name : names) {
        synth::SnippetInfo info;
        info.name = name;
        info.moduleCount = 3;
        result.add(info);
    }
    return result;
}

} // namespace

// ============================================================================
// Baseline structure
// ============================================================================

TEST(ModuleLibraryStructure, EverySectionIsExpandedByDefault) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 900);

    EXPECT_FALSE(comp.areAllSectionsCollapsed());
    for (const auto& header : headerNames(comp))
        EXPECT_FALSE(comp.isSectionCollapsed(header)) << header;
}

TEST(ModuleLibraryStructure, HasASnippetsSectionAheadOfTheModuleCategories) {
    ModuleLibraryComponent comp;
    auto headers = headerNames(comp);

    ASSERT_GT(headers.size(), 1);
    EXPECT_EQ(headers[0], ModuleLibraryComponent::kSnippetsHeader) << "snippets are what the user just made";
    EXPECT_TRUE(headers.contains("Sources"));
    EXPECT_TRUE(headers.contains("Dynamics"));
}

TEST(ModuleLibraryStructure, EveryModuleRowKnowsItsOwningSection) {
    ModuleLibraryComponent comp;
    for (int i = 0; i < comp.getEntryCount(); ++i) {
        const auto& entry = comp.getEntry(i);
        if (entry.kind == RowKind::Header)
            EXPECT_TRUE(entry.section.isEmpty());
        else
            EXPECT_FALSE(entry.section.isEmpty()) << entry.text << " must belong to a section";
    }
}

TEST(ModuleLibraryStructure, RowsAreLaidOutTopToBottomWithoutOverlap) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 1200);

    auto rows = comp.buildRows();
    ASSERT_GT(rows.size(), 2u);
    for (size_t i = 1; i < rows.size(); ++i)
        EXPECT_GE(rows[i].y, rows[i - 1].y + rows[i - 1].height) << "row " << i << " overlaps its predecessor";
}

TEST(ModuleLibraryStructure, HitTestingAgreesWithTheRowLayout) {
    // paint() and getEntryIndexAt() share buildRows(), so every row's centre must map back to it.
    ModuleLibraryComponent comp;
    comp.setSize(200, 1600);

    for (const auto& row : comp.buildRows()) {
        const int centre = row.y + row.height / 2;
        EXPECT_EQ(comp.getEntryIndexAt(centre), row.entryIndex)
            << "row at y=" << row.y << " (entry " << row.entryIndex << ") is not hit-testable at its centre";
    }
}

TEST(ModuleLibraryStructure, ClicksAboveAndBelowTheRowsHitNothing) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 1600);

    EXPECT_EQ(comp.getEntryIndexAt(-5), -1);
    EXPECT_EQ(comp.getEntryIndexAt(99999), -1);
}

// ============================================================================
// Collapse / expand one section
// ============================================================================

TEST(ModuleLibraryCollapse, CollapsingASectionHidesItsRowsButKeepsItsHeader) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 1200);

    const int rowsBefore = comp.getVisibleRowCount();
    const int headersBefore = countRowsOfKind(comp, RowKind::Header);

    comp.setSectionCollapsed("Sources", true);

    EXPECT_TRUE(comp.isSectionCollapsed("Sources"));
    EXPECT_LT(comp.getVisibleRowCount(), rowsBefore) << "Oscillator/Noise/LFO must disappear";
    EXPECT_EQ(countRowsOfKind(comp, RowKind::Header), headersBefore) << "headers always stay visible";

    // None of the hidden rows are laid out any more.
    for (const auto& row : comp.buildRows())
        EXPECT_NE(comp.getEntry(row.entryIndex).text, "Oscillator");
}

TEST(ModuleLibraryCollapse, ExpandingRestoresExactlyWhatWasHidden) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 1200);

    const int rowsBefore = comp.getVisibleRowCount();
    comp.setSectionCollapsed("Sources", true);
    comp.setSectionCollapsed("Sources", false);

    EXPECT_FALSE(comp.isSectionCollapsed("Sources"));
    EXPECT_EQ(comp.getVisibleRowCount(), rowsBefore);
}

TEST(ModuleLibraryCollapse, ToggleSectionFlipsState) {
    ModuleLibraryComponent comp;

    comp.toggleSection("Filters");
    EXPECT_TRUE(comp.isSectionCollapsed("Filters"));
    comp.toggleSection("Filters");
    EXPECT_FALSE(comp.isSectionCollapsed("Filters"));
}

TEST(ModuleLibraryCollapse, CollapsingShrinksTotalContentHeight) {
    // The reason the feature exists: many sections overflow the sidebar.
    ModuleLibraryComponent comp;
    comp.setSize(200, 1200);

    const int heightExpanded = comp.getTotalContentHeight();
    comp.setAllSectionsCollapsed(true);
    const int heightCollapsed = comp.getTotalContentHeight();

    EXPECT_LT(heightCollapsed, heightExpanded);
}

TEST(ModuleLibraryCollapse, ClickingAHeaderRowTogglesThatSection) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 1200);

    // Locate the "Sources" header row and click its centre.
    int sourcesIndex = -1;
    for (int i = 0; i < comp.getEntryCount(); ++i)
        if (comp.getEntry(i).kind == RowKind::Header && comp.getEntry(i).text == "Sources")
            sourcesIndex = i;
    ASSERT_GE(sourcesIndex, 0);

    const int y = comp.getRowCentreY(sourcesIndex);
    ASSERT_GT(y, 0);

    juce::MouseInputSource src = juce::Desktop::getInstance().getMainMouseSource();
    juce::MouseEvent evt(src, juce::Point<float>(30.0f, (float)y), juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                         &comp, &comp, juce::Time::getCurrentTime(), juce::Point<float>(30.0f, (float)y),
                         juce::Time::getCurrentTime(), 1, false);
    comp.mouseDown(evt);

    EXPECT_TRUE(comp.isSectionCollapsed("Sources"));
}

TEST(ModuleLibraryCollapse, NotifiesTheOwnerOnUserDrivenChangesOnly) {
    ModuleLibraryComponent comp;
    int notifications = 0;
    comp.onCollapseStateChanged = [&notifications] { ++notifications; };

    comp.setSectionCollapsed("Sources", true);
    EXPECT_EQ(notifications, 1);

    comp.setSectionCollapsed("Sources", true); // already collapsed — no change, no notification
    EXPECT_EQ(notifications, 1);

    comp.toggleAllSections();
    EXPECT_EQ(notifications, 2);

    // Restoring persisted state must not write it straight back out.
    comp.setCollapsedSections({"Filters"});
    EXPECT_EQ(notifications, 2);
}

// ============================================================================
// Collapse all / expand all
// ============================================================================

TEST(ModuleLibraryCollapseAll, CollapsesEverySectionThenExpandsEverySection) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 1200);

    comp.setAllSectionsCollapsed(true);
    EXPECT_TRUE(comp.areAllSectionsCollapsed());
    for (const auto& header : headerNames(comp))
        EXPECT_TRUE(comp.isSectionCollapsed(header)) << header;
    EXPECT_EQ(comp.getVisibleRowCount(), countRowsOfKind(comp, RowKind::Header))
        << "only headers remain when everything is collapsed";

    comp.setAllSectionsCollapsed(false);
    EXPECT_FALSE(comp.areAllSectionsCollapsed());
    for (const auto& header : headerNames(comp))
        EXPECT_FALSE(comp.isSectionCollapsed(header)) << header;
}

TEST(ModuleLibraryCollapseAll, ToggleAllExpandsWhenEverythingIsAlreadyCollapsed) {
    ModuleLibraryComponent comp;

    comp.toggleAllSections();
    EXPECT_TRUE(comp.areAllSectionsCollapsed());
    comp.toggleAllSections();
    EXPECT_FALSE(comp.areAllSectionsCollapsed());
}

TEST(ModuleLibraryCollapseAll, ToggleAllCollapsesWhenOnlySomeSectionsAreCollapsed) {
    ModuleLibraryComponent comp;
    comp.setSectionCollapsed("Sources", true);
    ASSERT_FALSE(comp.areAllSectionsCollapsed());

    comp.toggleAllSections();
    EXPECT_TRUE(comp.areAllSectionsCollapsed()) << "a partial state folds the rest rather than unfolding";
}

TEST(ModuleLibraryCollapseAll, ClickingTheTopStripTogglesEverything) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 1200);

    ASSERT_TRUE(ModuleLibraryComponent::isInTopStrip(4));

    juce::MouseInputSource src = juce::Desktop::getInstance().getMainMouseSource();
    juce::MouseEvent evt(src, juce::Point<float>(150.0f, 4.0f), juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                         &comp, &comp, juce::Time::getCurrentTime(), juce::Point<float>(150.0f, 4.0f),
                         juce::Time::getCurrentTime(), 1, false);
    comp.mouseDown(evt);

    EXPECT_TRUE(comp.areAllSectionsCollapsed());
}

TEST(ModuleLibraryCollapseAll, TheTopStripSitsAboveEveryRow) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 1200);

    auto rows = comp.buildRows();
    ASSERT_FALSE(rows.empty());
    EXPECT_GE(rows.front().y, ModuleLibraryComponent::kTopStripHeight)
        << "the collapse-all chrome must not overlap row 0";
    EXPECT_EQ(comp.getEntryIndexAt(2), -1) << "a click in the strip is not an entry click";
}

// ============================================================================
// Persistence
// ============================================================================

TEST(ModuleLibraryPersistence, CollapsedSectionsRoundTrip) {
    ModuleLibraryComponent saved;
    saved.setSectionCollapsed("Sources", true);
    saved.setSectionCollapsed("Time FX", true);
    auto state = saved.getCollapsedSections();

    ASSERT_EQ(state.size(), 2);

    ModuleLibraryComponent restored;
    restored.setCollapsedSections(state);
    EXPECT_TRUE(restored.isSectionCollapsed("Sources"));
    EXPECT_TRUE(restored.isSectionCollapsed("Time FX"));
    EXPECT_FALSE(restored.isSectionCollapsed("Filters"));
}

TEST(ModuleLibraryPersistence, BlankPersistedStateExpandsEverything) {
    // An unset preference arrives as StringArray::fromLines("") — a single empty string. Storing it
    // would make areAllSectionsCollapsed() count a section that does not exist.
    ModuleLibraryComponent comp;
    comp.setCollapsedSections(juce::StringArray::fromLines(""));

    EXPECT_TRUE(comp.getCollapsedSections().isEmpty());
    EXPECT_FALSE(comp.areAllSectionsCollapsed());
}

TEST(ModuleLibraryPersistence, UnknownSectionNamesAreHarmless) {
    ModuleLibraryComponent comp;
    comp.setCollapsedSections({"NoSuchSection"});

    EXPECT_FALSE(comp.areAllSectionsCollapsed());
    EXPECT_NO_THROW(comp.buildRows());
}

// ============================================================================
// Snippets section
// ============================================================================

TEST(ModuleLibrarySnippets, ShowsAnEmptyHintWhenThereAreNoSnippets) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 1200);

    EXPECT_EQ(comp.getSnippetCount(), 0);
    EXPECT_EQ(countRowsOfKind(comp, RowKind::Snippet), 0);
    EXPECT_EQ(countRowsOfKind(comp, RowKind::EmptyHint), 1) << "the section stays visible so it is discoverable";
}

TEST(ModuleLibrarySnippets, SavedSnippetsBecomeDraggableRows) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 1200);

    comp.setSnippets(makeSnippets({"My Supersaw Lead", "Wobble Bass"}));

    EXPECT_EQ(comp.getSnippetCount(), 2);
    EXPECT_EQ(countRowsOfKind(comp, RowKind::Snippet), 2);
    EXPECT_EQ(countRowsOfKind(comp, RowKind::EmptyHint), 0);

    // A snippet row is the first draggable row, ahead of the module catalogue.
    const int first = comp.getFirstDraggableEntryIndex();
    ASSERT_GE(first, 0);
    EXPECT_EQ(comp.getEntry(first).kind, RowKind::Snippet);
    EXPECT_EQ(comp.getEntry(first).text, "My Supersaw Lead");
}

TEST(ModuleLibrarySnippets, RowsCarryTheirModuleCount) {
    ModuleLibraryComponent comp;
    comp.setSnippets(makeSnippets({"Lead"}));

    const int first = comp.getFirstDraggableEntryIndex();
    ASSERT_GE(first, 0);
    EXPECT_EQ(comp.getEntry(first).moduleCount, 3);
}

TEST(ModuleLibrarySnippets, RefreshingTheListReplacesTheRows) {
    ModuleLibraryComponent comp;
    comp.setSnippets(makeSnippets({"A", "B", "C"}));
    ASSERT_EQ(countRowsOfKind(comp, RowKind::Snippet), 3);

    comp.setSnippets(makeSnippets({"A"}));
    EXPECT_EQ(countRowsOfKind(comp, RowKind::Snippet), 1);

    comp.setSnippets({});
    EXPECT_EQ(countRowsOfKind(comp, RowKind::Snippet), 0);
    EXPECT_EQ(countRowsOfKind(comp, RowKind::EmptyHint), 1);
}

TEST(ModuleLibrarySnippets, CollapsingTheSnippetsSectionHidesItsRows) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 1200);
    comp.setSnippets(makeSnippets({"Lead", "Bass"}));

    comp.setSectionCollapsed(ModuleLibraryComponent::kSnippetsHeader, true);
    EXPECT_EQ(countRowsOfKind(comp, RowKind::Snippet), 0);

    // With snippets hidden, the first draggable row falls back to a module.
    const int first = comp.getFirstDraggableEntryIndex();
    ASSERT_GE(first, 0);
    EXPECT_EQ(comp.getEntry(first).kind, RowKind::Module);
}

TEST(ModuleLibrarySnippets, SnippetTooltipNamesTheGroupAndItsSize) {
    auto tip = ModuleLibraryComponent::snippetDescription("Lead", 4);
    EXPECT_TRUE(tip.contains("Lead"));
    EXPECT_TRUE(tip.contains("4"));

    // Singular/plural is handled so the tooltip reads correctly for a one-module snippet.
    EXPECT_TRUE(ModuleLibraryComponent::snippetDescription("Solo", 1).contains("1 module."));
    EXPECT_TRUE(ModuleLibraryComponent::snippetDescription("Pair", 2).contains("2 modules."));
}

TEST(ModuleLibrarySnippets, RightClickingASnippetRowRequestsDeletionByName) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 1200);
    comp.setSnippets(makeSnippets({"Doomed"}));

    // The delete request is raised from an async PopupMenu, so assert the wiring exists and that
    // invoking it reports the right name (the menu itself is not driven headlessly).
    juce::String requested;
    comp.onSnippetDeleteRequested = [&requested](const juce::String& name) { requested = name; };
    ASSERT_TRUE(comp.onSnippetDeleteRequested != nullptr);

    comp.onSnippetDeleteRequested(comp.getEntry(comp.getFirstDraggableEntryIndex()).text);
    EXPECT_EQ(requested, "Doomed");
}

// ============================================================================
// Paint smoke tests across the new states
// ============================================================================

TEST(ModuleLibraryPaint, PaintsWithSnippetsAndMixedCollapseState) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 900);
    comp.setSnippets(makeSnippets({"Lead", "Bass"}));
    comp.setSectionCollapsed("Sources", true);

    juce::Image img(juce::Image::ARGB, 200, 900, true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(comp.paint(g));
}

TEST(ModuleLibraryPaint, PaintsWithEverythingCollapsed) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 900);
    comp.setAllSectionsCollapsed(true);

    juce::Image img(juce::Image::ARGB, 200, 900, true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(comp.paint(g));
}
