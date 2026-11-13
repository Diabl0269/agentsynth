// ModuleLibrarySearchTests.cpp
// Tests for the library sidebar search field:
//   • filter        — non-matching module/snippet rows disappear; empty sections drop out
//   • sections      — matching categories stay visible and open even when they were collapsed
//   • chrome        — the search editor is pinned above Collapse all; filtering does not persist
//                     a collapse the user never asked for
//   • theme         — the searchEditor's cached colours must not go stale between construction
//                     (against whatever theme was active then) and being parented (which is
//                     always after MainComponent applies the final persisted theme)

#include "UI/Library/ModuleLibraryComponent/ModuleLibraryComponent.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/BuiltInThemes.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

using RowKind = ModuleLibraryComponent::RowKind;

namespace {

juce::StringArray visibleTextsOfKind(const ModuleLibraryComponent& comp, RowKind kind) {
    juce::StringArray names;
    for (const auto& row : comp.buildRows()) {
        const auto& entry = comp.getEntry(row.entryIndex);
        if (entry.kind == kind)
            names.add(entry.text);
    }
    return names;
}

juce::StringArray visibleHeaders(const ModuleLibraryComponent& comp) {
    return visibleTextsOfKind(comp, RowKind::Header);
}

juce::StringArray visibleModules(const ModuleLibraryComponent& comp) {
    return visibleTextsOfKind(comp, RowKind::Module);
}

juce::Array<synth::SnippetInfo> makeSnippets(const juce::StringArray& names) {
    juce::Array<synth::SnippetInfo> result;
    for (const auto& name : names) {
        synth::SnippetInfo info;
        info.name = name;
        info.moduleCount = 2;
        result.add(info);
    }
    return result;
}

juce::TextEditor* findSearchEditor(ModuleLibraryComponent& comp) {
    for (int i = 0; i < comp.getNumChildComponents(); ++i)
        if (auto* editor = dynamic_cast<juce::TextEditor*>(comp.getChildComponent(i)))
            return editor;
    return nullptr;
}

} // namespace

// ============================================================================
// Query matching (pure, no layout)
// ============================================================================

TEST(ModuleLibrarySearchMatch, BlankAndWhitespaceQueriesAreInactive) {
    EXPECT_TRUE(ModuleLibraryComponent::normalisedSearchQuery({}).isEmpty());
    EXPECT_TRUE(ModuleLibraryComponent::normalisedSearchQuery("   ").isEmpty());
}

// Matching and highlight spans are the shared synth::ui::searchMatches / searchHighlightSpans
// (Tests/UI/Layout/SearchMatchTests.cpp).

// ============================================================================
// Filter behaviour
// ============================================================================

TEST(ModuleLibrarySearchFilter, EmptyQueryShowsTheFullCatalogue) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 1600);

    const int rowsBefore = comp.getVisibleRowCount();
    comp.setSearchText({});
    EXPECT_FALSE(comp.isSearchActive());
    EXPECT_EQ(comp.getVisibleRowCount(), rowsBefore);
    EXPECT_TRUE(visibleModules(comp).contains("Oscillator"));
    EXPECT_TRUE(visibleModules(comp).contains("Reverb"));
}

TEST(ModuleLibrarySearchFilter, QueryHidesNonMatchingModules) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 1600);
    comp.setSearchText("osc");

    EXPECT_TRUE(comp.isSearchActive());
    const auto modules = visibleModules(comp);
    EXPECT_TRUE(modules.contains("Oscillator"));
    EXPECT_FALSE(modules.contains("Reverb"));
    EXPECT_FALSE(modules.contains("Filter"));
    EXPECT_EQ(modules.size(), 1);
}

// Other synths' names for a module find it; the row keeps its real name.
TEST(ModuleLibrarySearchFilter, AnAliasFindsTheModuleUnderItsRealName) {
    for (const char* query : {"env", "envelope", "eg", "contour"}) {
        ModuleLibraryComponent comp;
        comp.setSize(200, 1600);
        comp.setSearchText(query);
        EXPECT_TRUE(visibleModules(comp).contains("ADSR")) << query;
    }
    ModuleLibraryComponent comp;
    comp.setSize(200, 1600);
    comp.setSearchText("vco");
    EXPECT_TRUE(visibleModules(comp).contains("Oscillator"));
    comp.setSearchText("vcf");
    EXPECT_TRUE(visibleModules(comp).contains("Filter"));
}

// Every word must appear, in any order: the library matches like every other search box.
TEST(ModuleLibrarySearchFilter, MultiWordQueryMatchesInAnyOrder) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 1600);
    comp.setSearchText("eq para");

    const auto modules = visibleModules(comp);
    EXPECT_TRUE(modules.contains("Parametric EQ"));
    EXPECT_EQ(modules.size(), 1);
}

TEST(ModuleLibrarySearchFilter, WhitespaceAroundTheQueryIsIgnored) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 1600);
    comp.setSearchText("  OSC  ");

    EXPECT_TRUE(comp.isSearchActive());
    EXPECT_TRUE(visibleModules(comp).contains("Oscillator"));
    EXPECT_EQ(visibleModules(comp).size(), 1);
}

TEST(ModuleLibrarySearchFilter, NonMatchingSectionsDisappear) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 1600);
    comp.setSearchText("osc");

    const auto headers = visibleHeaders(comp);
    EXPECT_TRUE(headers.contains("Sources"));
    EXPECT_FALSE(headers.contains("Time FX"));
    EXPECT_FALSE(headers.contains("Dynamics"));
    EXPECT_FALSE(headers.contains(ModuleLibraryComponent::kSnippetsHeader));
}

TEST(ModuleLibrarySearchFilter, HeaderMatchRevealsEveryModuleInThatSection) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 1600);
    comp.setSearchText("Time");

    const auto modules = visibleModules(comp);
    EXPECT_TRUE(modules.contains("Delay"));
    EXPECT_TRUE(modules.contains("Reverb"));
    EXPECT_EQ(modules.size(), 2);
    EXPECT_TRUE(visibleHeaders(comp).contains("Time FX"));
}

TEST(ModuleLibrarySearchFilter, MatchingSectionsOpenEvenWhenTheyWereCollapsed) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 1600);
    comp.setSectionCollapsed("Sources", true);
    ASSERT_TRUE(comp.isSectionCollapsed("Sources"));

    int oscillatorIndex = -1;
    for (int i = 0; i < comp.getEntryCount(); ++i)
        if (comp.getEntryText(i) == "Oscillator")
            oscillatorIndex = i;
    ASSERT_GE(oscillatorIndex, 0);
    EXPECT_EQ(comp.getRowCentreY(oscillatorIndex), -1) << "collapsed Sources must hide Oscillator";

    comp.setSearchText("osc");
    EXPECT_TRUE(comp.isSectionCollapsed("Sources")) << "search must not rewrite the stored fold";
    EXPECT_GT(comp.getRowCentreY(oscillatorIndex), 0) << "the match must be visible while searching";
}

TEST(ModuleLibrarySearchFilter, ClearingSearchRestoresCollapsedSections) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 1600);
    comp.setSectionCollapsed("Sources", true);

    int oscillatorIndex = -1;
    for (int i = 0; i < comp.getEntryCount(); ++i)
        if (comp.getEntryText(i) == "Oscillator")
            oscillatorIndex = i;
    ASSERT_GE(oscillatorIndex, 0);

    comp.setSearchText("osc");
    ASSERT_GT(comp.getRowCentreY(oscillatorIndex), 0);

    comp.setSearchText({});
    EXPECT_TRUE(comp.isSectionCollapsed("Sources"));
    EXPECT_EQ(comp.getRowCentreY(oscillatorIndex), -1);
}

TEST(ModuleLibrarySearchFilter, SearchDoesNotNotifyCollapseListeners) {
    ModuleLibraryComponent comp;
    int fires = 0;
    comp.onCollapseStateChanged = [&] { ++fires; };

    comp.setSearchText("osc");
    comp.setSearchText("filter");
    comp.setSearchText({});
    EXPECT_EQ(fires, 0);
}

TEST(ModuleLibrarySearchFilter, CatalogueNamesStayCompleteWhileFiltered) {
    ModuleLibraryComponent comp;
    const auto before = comp.getDraggableModuleNames();
    comp.setSearchText("osc");
    EXPECT_EQ(comp.getDraggableModuleNames(), before)
        << "getDraggableModuleNames feeds the factory; a visual filter must not shrink it";
}

TEST(ModuleLibrarySearchFilter, SnippetNamesAreSearchable) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 1600);
    comp.setSnippets(makeSnippets({"Bass sting", "Pad wash"}));
    comp.setSearchText("pad");

    EXPECT_TRUE(visibleTextsOfKind(comp, RowKind::Snippet).contains("Pad wash"));
    EXPECT_FALSE(visibleTextsOfKind(comp, RowKind::Snippet).contains("Bass sting"));
    EXPECT_TRUE(visibleHeaders(comp).contains(ModuleLibraryComponent::kSnippetsHeader));
}

TEST(ModuleLibrarySearchFilter, EmptyHintIsHiddenUnlessTheSnippetsHeaderMatches) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 1600);
    ASSERT_TRUE(visibleTextsOfKind(comp, RowKind::EmptyHint).contains("No snippets yet"));

    comp.setSearchText("osc");
    EXPECT_TRUE(visibleTextsOfKind(comp, RowKind::EmptyHint).isEmpty());

    comp.setSearchText("snip");
    EXPECT_TRUE(visibleHeaders(comp).contains(ModuleLibraryComponent::kSnippetsHeader));
    EXPECT_TRUE(visibleTextsOfKind(comp, RowKind::EmptyHint).contains("No snippets yet"));
}

TEST(ModuleLibrarySearchFilter, NoMatchLeavesNoRows) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 1600);
    comp.setSearchText("xyzzy-no-such-module");

    EXPECT_TRUE(comp.isSearchActive());
    EXPECT_EQ(comp.getVisibleRowCount(), 0);
    EXPECT_TRUE(visibleModules(comp).isEmpty());
}

TEST(ModuleLibrarySearchFilter, HitTestingAgreesWithFilteredRows) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 1600);
    comp.setSearchText("midi");

    auto rows = comp.buildRows();
    ASSERT_FALSE(rows.empty());
    for (size_t i = 1; i < rows.size(); ++i)
        EXPECT_GE(rows[i].y, rows[i - 1].y + rows[i - 1].height) << "filtered row " << i << " overlaps";

    for (const auto& row : rows) {
        const int centre = row.y + row.height / 2;
        EXPECT_EQ(comp.getEntryIndexAt(centre), row.entryIndex);
    }
}

TEST(ModuleLibrarySearchFilter, ScrollBarHidesWhenTheFilterFits) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 400);
    ASSERT_TRUE(comp.isScrollBarVisible());

    comp.setSearchText("oscillator");
    EXPECT_FALSE(comp.isScrollBarVisible()) << "one matching module must fit a 400 px panel";
    EXPECT_EQ(comp.getScrollOffset(), 0);
}

TEST(ModuleLibrarySearchFilter, SearchResetsScrollToTheTop) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 400);
    comp.setScrollOffset(comp.getMaxScrollOffset());
    ASSERT_GT(comp.getScrollOffset(), 0);

    comp.setSearchText("filter");
    EXPECT_EQ(comp.getScrollOffset(), 0);
}

// ============================================================================
// Chrome + paint
// ============================================================================

TEST(ModuleLibrarySearchChrome, SearchEditorIsPinnedAboveTheCollapseStrip) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 400);

    auto* editor = findSearchEditor(comp);
    ASSERT_NE(editor, nullptr);
    EXPECT_GE(editor->getY(), 0);
    EXPECT_LT(editor->getBottom(), ModuleLibraryComponent::kSearchHeight);
    EXPECT_LT(editor->getBottom(), ModuleLibraryComponent::kPinnedChromeHeight);
    EXPECT_TRUE(editor->isVisible());
}

TEST(ModuleLibrarySearchChrome, SetSearchTextRoundTripsThroughTheEditor) {
    ModuleLibraryComponent comp;
    comp.setSearchText("lfo");
    EXPECT_EQ(comp.getSearchText(), "lfo");
    EXPECT_EQ(findSearchEditor(comp)->getText(), "lfo");
}

TEST(ModuleLibrarySearchPaint, PaintWithActiveSearchDoesNotCrash) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 400);
    comp.setSearchText("osc");

    juce::Image img(juce::Image::ARGB, 200, 400, true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(comp.paint(g));
}

TEST(ModuleLibrarySearchPaint, PaintWithNoMatchesDoesNotCrash) {
    ModuleLibraryComponent comp;
    comp.setSize(200, 400);
    comp.setSearchText("xyzzy-no-such-module");

    juce::Image img(juce::Image::ARGB, 200, 400, true);
    juce::Graphics g(img);
    EXPECT_NO_THROW(comp.paint(g));
}

// ============================================================================
// Theme colour staleness (bug fix: parentHierarchyChanged())
//
// Reproduces MainComponent's real ordering: moduleLibrary is a plain member, constructed
// against whatever theme is the Desktop default AT THAT POINT — the built-in default theme
// Main.cpp primes before any Component exists. MainComponent's ctor BODY then applies the
// persisted theme to that SAME LookAndFeel instance, without a live sendLookAndFeelChange()
// broadcast (that only happens on a user-driven theme switch later). The searchEditor's cached
// TextEditor colours must therefore only be trusted once the component is actually parented.
// ============================================================================

TEST(ModuleLibrarySearchTheme, ColoursMatchWhicheverThemeWasActiveAtConstruction) {
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(synth::theme::makeObsidian());
    juce::Desktop::getInstance().setDefaultLookAndFeel(&lf);

    ModuleLibraryComponent comp;
    auto* editor = findSearchEditor(comp);
    ASSERT_NE(editor, nullptr);
    EXPECT_EQ(editor->findColour(juce::TextEditor::backgroundColourId), synth::theme::makeObsidian().colors.surface);

    juce::Desktop::getInstance().setDefaultLookAndFeel(nullptr);
}

TEST(ModuleLibrarySearchTheme, UnparentedColoursDoNotAutoUpdateAfterTheLnFIsMutated) {
    // Negative control: without a parent (so parentHierarchyChanged() never fires) and without a
    // live sendLookAndFeelChange() broadcast (so lookAndFeelChanged() never fires either), the
    // colours cached at construction must stay exactly as they were — proving those two hooks are
    // the ONLY triggers, with no polling anywhere.
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(synth::theme::makeObsidian());
    juce::Desktop::getInstance().setDefaultLookAndFeel(&lf);

    ModuleLibraryComponent comp;
    auto* editor = findSearchEditor(comp);
    ASSERT_NE(editor, nullptr);
    ASSERT_EQ(editor->findColour(juce::TextEditor::backgroundColourId), synth::theme::makeObsidian().colors.surface);

    // Simulate MainComponent's ctor body applying the FINAL persisted theme to the SAME LnF
    // instance — no sendLookAndFeelChange() call, exactly like AppLookAndFeel::applyTheme().
    lf.applyTheme(synth::theme::makeNeon());

    EXPECT_EQ(editor->findColour(juce::TextEditor::backgroundColourId), synth::theme::makeObsidian().colors.surface)
        << "still unparented — must remain stale until parented or told to re-look-and-feel";

    juce::Desktop::getInstance().setDefaultLookAndFeel(nullptr);
}

TEST(ModuleLibrarySearchTheme, ParentingRepullsColoursOntoTheFinalPersistedTheme) {
    synth::theme::AppLookAndFeel lf;
    lf.applyTheme(synth::theme::makeObsidian());
    juce::Desktop::getInstance().setDefaultLookAndFeel(&lf);

    ModuleLibraryComponent comp;
    auto* editor = findSearchEditor(comp);
    ASSERT_NE(editor, nullptr);
    ASSERT_EQ(editor->findColour(juce::TextEditor::backgroundColourId), synth::theme::makeObsidian().colors.surface);

    // MainComponent's ctor body corrects the LnF to the persisted theme before moduleLibrary is
    // ever parented (addAndMakeVisible always runs after that call in every ctor path).
    lf.applyTheme(synth::theme::makeNeon());

    juce::Component parent;
    parent.addAndMakeVisible(comp);

    EXPECT_EQ(editor->findColour(juce::TextEditor::backgroundColourId), synth::theme::makeNeon().colors.surface)
        << "parentHierarchyChanged() must re-pull colours from the now-final theme";

    juce::Desktop::getInstance().setDefaultLookAndFeel(nullptr);
}
