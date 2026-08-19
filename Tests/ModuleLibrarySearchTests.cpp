// ModuleLibrarySearchTests.cpp
// Tests for the library sidebar search field:
//   • filter        — non-matching module/snippet rows disappear; empty sections drop out
//   • sections      — matching categories stay visible and open even when they were collapsed
//   • highlight     — highlightSpansFor reports each case-insensitive hit used when painting
//   • chrome        — the search editor is pinned above COLLAPSE ALL; filtering does not persist
//                     a collapse the user never asked for

#include "../Source/UI/ModuleLibraryComponent.h"
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
    EXPECT_FALSE(ModuleLibraryComponent::textMatchesQuery("Oscillator", {}));
    EXPECT_FALSE(ModuleLibraryComponent::textMatchesQuery("Oscillator", "   "));
}

TEST(ModuleLibrarySearchMatch, MatchingIsCaseInsensitiveAndSubstring) {
    EXPECT_TRUE(ModuleLibraryComponent::textMatchesQuery("Oscillator", "osc"));
    EXPECT_TRUE(ModuleLibraryComponent::textMatchesQuery("Oscillator", "OSC"));
    EXPECT_TRUE(ModuleLibraryComponent::textMatchesQuery("Parametric EQ", "eq"));
    EXPECT_FALSE(ModuleLibraryComponent::textMatchesQuery("Filter", "reverb"));
}

TEST(ModuleLibrarySearchHighlight, SpansAreEmptyWhenTheQueryIsBlank) {
    EXPECT_TRUE(ModuleLibraryComponent::highlightSpansFor("Oscillator", {}).empty());
    EXPECT_TRUE(ModuleLibraryComponent::highlightSpansFor("Oscillator", "   ").empty());
}

TEST(ModuleLibrarySearchHighlight, SpansCoverEachNonOverlappingHit) {
    const auto spans = ModuleLibraryComponent::highlightSpansFor("Oscillator", "osc");
    ASSERT_EQ(spans.size(), 1u);
    EXPECT_EQ(spans[0].start, 0);
    EXPECT_EQ(spans[0].length, 3);

    const auto mid = ModuleLibraryComponent::highlightSpansFor("Parametric EQ", "eq");
    ASSERT_EQ(mid.size(), 1u);
    EXPECT_EQ(mid[0].start, 11);
    EXPECT_EQ(mid[0].length, 2);

    const auto repeated = ModuleLibraryComponent::highlightSpansFor("Midi MIDI", "midi");
    ASSERT_EQ(repeated.size(), 2u);
    EXPECT_EQ(repeated[0].start, 0);
    EXPECT_EQ(repeated[1].start, 5);
}

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
