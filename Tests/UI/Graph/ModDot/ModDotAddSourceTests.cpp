// The mod dot's panel, Add source page: the groups it offers, the search, "added" sources, the folds, picking one
// (a routing at +25 percent, one undo step, back on the sources page with the new row selected) and Esc stepping
// back. Plus the one pin that this page and the Mod Matrix offer the same sources.
// docs/modules/modulation.md#the-mod-dot-menu.

#include "ModDotTestFixture.h"

#include "AudioEngine/ModuleTitle.h"
#include "Modules/ADSRModule.h"
#include "Modules/FilterModule.h"
#include "Modules/MacroControlModule.h"
#include "UI/Graph/ModDot/ModSourceCatalog.h"
#include "UI/Graph/ModMatrixComponent.h"
#include "UI/Layout/ReducedMotion.h"

namespace {

using synth::ui::ModDotAddSourcePage;
using synth::ui::ModDotChoiceRow;
using synth::ui::ModDotPopover;

struct NoMotion {
    NoMotion() { synth::ui::setReducedMotionForTest(true); }
    ~NoMotion() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

// Types `text` into the search field one key at a time, then delivers the change the editor posts as a message.
void typeInto(juce::TextEditor& editor, const juce::String& text) {
    for (auto c : text)
        editor.keyPressed(juce::KeyPress((int)c, juce::ModifierKeys(), c));
    editor.onTextChange();
}

juce::AudioProcessorGraph::NodeID addNode(juce::AudioProcessorGraph& graph, std::unique_ptr<juce::AudioProcessor> p) {
    return graph.addNode(std::move(p))->nodeID;
}

// The fixture plus a second LFO nobody uses, an envelope and a macro bank, with the panel on its Add source page.
struct AddFixture : Fixture {
    juce::AudioProcessorGraph::NodeID lfo2, adsr, macros;
    ModDotPopover* panel = nullptr;

    AddFixture() {
        auto& graph = engine.getGraph();
        lfo2 = addNode(graph, std::make_unique<LFOModule>());
        adsr = addNode(graph, std::make_unique<ADSRModule>());
        macros = addNode(graph, std::make_unique<MacroControlModule>());
        refresh();
        panel = clickDot();
    }

    ModDotAddSourcePage& add() { return panel->addSourcePage(); }
    void openAdd() { clickNow(panel->sourcesPage().addButton()); }
    juce::String titleOf(juce::AudioProcessorGraph::NodeID id) {
        return synth::moduleTitle(*engine.getGraph().getNodeForId(id));
    }
    ModDotChoiceRow* rowLabelled(const juce::String& label) {
        for (int i = 0; auto* row = add().visibleRow(i); ++i)
            if (row->item().label() == label)
                return row;
        return nullptr;
    }
};

} // namespace

TEST_F(ModuleComponentTest, AddSourceOffersTheGroupsWithSourcesAndGreysWhatIsAlreadyOnTheKnob) {
    NoMotion motion;
    AddFixture f;
    ASSERT_NE(f.panel, nullptr);
    f.openAdd();

    EXPECT_EQ(f.panel->page(), ModDotPopover::Page::AddSource);
    const std::vector<juce::String> groups{"LFOs", "Envelopes", "Macros"};
    EXPECT_EQ(f.add().visibleGroupNames(), groups) << "groups with nothing in them are left out";
    ASSERT_NE(f.rowLabelled(f.titleOf(f.lfoId)), nullptr);
    EXPECT_TRUE(f.rowLabelled(f.titleOf(f.lfoId))->isAdded());
    EXPECT_NE(f.rowLabelled(f.titleOf(f.lfoId))->getTitle().indexOf(", added"), -1) << "read out as added";
    ASSERT_NE(f.rowLabelled(f.titleOf(f.lfo2)), nullptr);
    EXPECT_FALSE(f.rowLabelled(f.titleOf(f.lfo2))->isAdded());
    for (const auto& label : f.add().visibleRowLabels())
        EXPECT_NE(label, f.titleOf(f.vcaId)) << "the card itself is never offered";
    EXPECT_EQ(f.add().backButton().getTitle(), "Back to sources");
    EXPECT_EQ(f.add().searchEditor().getTitle(), "Search sources");
    EXPECT_EQ(f.add().expandAllButton().getTitle(), "Expand all groups");
    EXPECT_EQ(f.add().collapseAllButton().getTitle(), "Collapse all groups");
}

TEST_F(ModuleComponentTest, TypingFiltersToMatchingRowsAndReturnAddsTheBestOneAtPlus25InOneUndoStep) {
    NoMotion motion;
    AddFixture f;
    f.openAdd();
    ASSERT_FALSE(f.undo.canUndo());

    typeInto(f.add().searchEditor(), "lfo");
    const auto labels = f.add().visibleRowLabels();
    ASSERT_FALSE(labels.empty());
    for (const auto& label : labels)
        EXPECT_TRUE(label.containsIgnoreCase("lfo")) << label;
    EXPECT_EQ(f.add().visibleGroupNames(), std::vector<juce::String>{"LFOs"});

    EXPECT_TRUE(f.add().searchEditor().keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));

    EXPECT_EQ(f.panel->page(), ModDotPopover::Page::Sources) << "back on the sources page";
    auto& sources = f.panel->sourcesPage();
    ASSERT_EQ(sources.rowCount(), 2);
    auto* added = sources.rowAt(1);
    EXPECT_EQ(added->source().sourceName, f.titleOf(f.lfo2));
    EXPECT_NEAR(f.amount(added->attenuverterId()), synth::ui::kModDotNewSourceDepth, 1e-4f);
    EXPECT_TRUE(added->isSelected());
    f.editor->timerCallback(); // the editor's tick refreshes the routing cache the choice is read against
    EXPECT_EQ(f.editor->getModDot().chosenAttenuverter(f.vcaId, f.gainChannel), added->attenuverterId());
    EXPECT_EQ(f.editor->getCachedModDisplayInfo().size(), 2u) << "the cable is drawn";

    ASSERT_TRUE(f.undo.undo());
    EXPECT_EQ(f.engine.getModulationRoutings().size(), 1u);
    EXPECT_FALSE(f.undo.canUndo()) << "the cable, its depth and the page change are one undo step";
}

TEST_F(ModuleComponentTest, AClickOnARowAddsItAndAnAddedSourceCannotBePickedTwice) {
    NoMotion motion;
    AddFixture f;
    f.openAdd();

    auto* already = f.rowLabelled(f.titleOf(f.lfoId));
    ASSERT_NE(already, nullptr);
    already->mouseUp(makeModuleClickWithMods(*already, {10, 5}, kPlain));
    already->keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
    EXPECT_EQ(f.panel->page(), ModDotPopover::Page::AddSource);
    EXPECT_EQ(f.engine.getModulationRoutings().size(), 1u);
    EXPECT_FALSE(f.undo.canUndo());

    auto* fresh = f.rowLabelled(f.titleOf(f.adsr));
    ASSERT_NE(fresh, nullptr);
    fresh->mouseUp(makeModuleClickWithMods(*fresh, {10, 5}, kPlain));
    EXPECT_EQ(f.engine.getModulationRoutings().size(), 2u);
    EXPECT_EQ(f.panel->page(), ModDotPopover::Page::Sources);
    f.openAdd();
    ASSERT_NE(f.rowLabelled(f.titleOf(f.adsr)), nullptr);
    EXPECT_TRUE(f.rowLabelled(f.titleOf(f.adsr))->isAdded());
}

TEST_F(ModuleComponentTest, ASearchWithNoHitSaysSoAndHidesEveryGroup) {
    NoMotion motion;
    AddFixture f;
    f.openAdd();
    typeInto(f.add().searchEditor(), "zzzz");
    EXPECT_TRUE(f.add().visibleGroupNames().empty());
    EXPECT_EQ(f.add().noMatchText(), "No source matches");
    EXPECT_TRUE(f.add().searchEditor().keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    EXPECT_EQ(f.panel->page(), ModDotPopover::Page::AddSource) << "nothing to add";
}

TEST_F(ModuleComponentTest, GroupsFoldByClickKeysAndTheExpandAndCollapseAllLinks) {
    NoMotion motion;
    AddFixture f;
    f.openAdd();
    auto* lfos = f.add().header("LFOs");
    ASSERT_NE(lfos, nullptr);
    EXPECT_EQ(lfos->getTitle(), "LFOs, expanded");

    clickNow(*lfos);
    EXPECT_FALSE(f.add().isGroupExpanded("LFOs"));
    EXPECT_EQ(lfos->getTitle(), "LFOs, collapsed");
    EXPECT_EQ(f.rowLabelled(f.titleOf(f.lfo2)), nullptr);

    EXPECT_TRUE(lfos->keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    EXPECT_TRUE(f.add().isGroupExpanded("LFOs"));
    EXPECT_TRUE(lfos->keyPressed(juce::KeyPress(juce::KeyPress::leftKey)));
    EXPECT_FALSE(f.add().isGroupExpanded("LFOs"));
    EXPECT_TRUE(lfos->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(30); // Return clicks, and a click is posted
    EXPECT_TRUE(f.add().isGroupExpanded("LFOs"));

    clickNow(f.add().collapseAllButton());
    EXPECT_TRUE(f.add().visibleRowLabels().empty());
    EXPECT_FALSE(f.add().isGroupExpanded("Envelopes"));
    clickNow(f.add().expandAllButton());
    EXPECT_FALSE(f.add().visibleRowLabels().empty());
}

TEST_F(ModuleComponentTest, SearchingOpensGroupsWithMatchesAndClearingPutsTheUsersFoldsBack) {
    NoMotion motion;
    AddFixture f;
    f.openAdd();
    clickNow(*f.add().header("LFOs"));
    clickNow(*f.add().header("Envelopes"));
    clickNow(*f.add().header("Macros"));
    ASSERT_TRUE(f.add().visibleRowLabels().empty());

    typeInto(f.add().searchEditor(), "lfo");
    EXPECT_TRUE(f.add().isGroupExpanded("LFOs")) << "a group with a match is open while searching";
    EXPECT_NE(f.rowLabelled(f.titleOf(f.lfo2)), nullptr);

    EXPECT_TRUE(f.add().searchEditor().keyPressed(juce::KeyPress(juce::KeyPress::escapeKey))); // clears
    EXPECT_EQ(f.add().searchEditor().getText(), "");
    EXPECT_EQ(f.panel->page(), ModDotPopover::Page::AddSource);
    EXPECT_FALSE(f.add().isGroupExpanded("LFOs"));
    EXPECT_FALSE(f.add().isGroupExpanded("Envelopes"));
}

TEST_F(ModuleComponentTest, EscapeStepsBackFromTheSearchToTheSourcesPageToClosed) {
    NoMotion motion;
    AddFixture f;
    int closed = 0;
    f.panel->onDismiss = [&] { ++closed; };
    f.openAdd();
    typeInto(f.add().searchEditor(), "lfo");

    EXPECT_TRUE(f.panel->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_EQ(f.add().searchEditor().getText(), "") << "first the search clears";
    EXPECT_EQ(f.panel->page(), ModDotPopover::Page::AddSource);
    EXPECT_TRUE(f.panel->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_EQ(f.panel->page(), ModDotPopover::Page::Sources) << "then the sources page";
    EXPECT_EQ(closed, 0);
    EXPECT_TRUE(f.panel->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_EQ(closed, 1) << "then closed";
    EXPECT_FALSE(f.undo.canUndo()) << "nothing was edited";
}

// The search field is what holds the keyboard on this page, so Escape must work through the editor's own key path
// (and the message it posts when it sees the key itself), not only through the panel's keyPressed.
TEST_F(ModuleComponentTest, EscapeInTheSearchFieldStepsBackThroughTheEditorsOwnKeyPath) {
    NoMotion motion;
    AddFixture f;
    int closed = 0;
    f.panel->onDismiss = [&] { ++closed; };
    f.openAdd();
    auto& search = f.add().searchEditor();
    typeInto(search, "lfo");
    ASSERT_NE(f.add().visibleRowLabels().size(), 3u) << "the query filtered the rows";

    EXPECT_TRUE(search.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_EQ(search.getText(), "") << "first Escape clears the search";
    EXPECT_EQ(f.panel->page(), ModDotPopover::Page::AddSource);
    EXPECT_TRUE(search.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_EQ(f.panel->page(), ModDotPopover::Page::Sources) << "second goes back to the sources page";
    EXPECT_EQ(closed, 0);

    // The editor's posted-message route lands on the same step.
    f.openAdd();
    typeInto(f.add().searchEditor(), "lfo");
    ASSERT_TRUE(f.add().searchEditor().onEscapeKey != nullptr);
    f.add().searchEditor().onEscapeKey();
    EXPECT_EQ(f.add().searchEditor().getText(), "");
}

// Growing to the Add source page must not move the callout to another side of the dot: the panel is capped to the
// room on the side it opened on and the list scrolls inside.
TEST_F(ModuleComponentTest, TheCalloutKeepsItsSideWhenTheAddSourcePageIsTallerThanTheRoom) {
    NoMotion motion;
    AddFixture f;
    for (int i = 0; i < 12; ++i)
        addNode(f.engine.getGraph(), std::make_unique<LFOModule>()); // enough rows to fill the page
    f.refresh();
    auto* panel = f.panel; // the dot was clicked in the fixture
    ASSERT_NE(panel, nullptr);

    juce::Component screen;
    screen.setBounds(0, 0, 600, 500);
    const juce::Rectangle<int> dot(100, 300, 12, 12);
    auto content = std::move(f.held);
    juce::CallOutBox box(*content, dot, &screen);
    ASSERT_GT(box.getBounds().getCentreY(), dot.getBottom()) << "opens under the dot";
    panel->keepSideOf(box, dot, screen.getLocalBounds());

    f.openAdd();
    EXPECT_GT(box.getBounds().getCentreY(), dot.getBottom()) << "still under the dot after the page grew";
    EXPECT_TRUE(screen.getLocalBounds().contains(box.getBounds())) << "and on screen";
    EXPECT_LE(panel->getHeight(),
              ModDotPopover::roomOnSide(box.getBounds(), dot, screen.getLocalBounds(), box.getBorderSize()));
    clickNow(f.add().backButton());
    EXPECT_GT(box.getBounds().getCentreY(), dot.getBottom()) << "and when it comes back";
    content.reset();
}

TEST_F(ModuleComponentTest, TheAddSourceListIsAsTallAsItsRowsUpToTheCap) {
    NoMotion motion;
    AddFixture f;
    f.openAdd();
    typeInto(f.add().searchEditor(), "lfo");
    const int searched = f.panel->getHeight();
    f.add().setQuery({});
    const int all = f.panel->getHeight();
    EXPECT_LT(searched, all) << "a search with few hits shrinks the panel";
    EXPECT_EQ(f.panel->getHeight(), f.add().preferredHeight());
    EXPECT_LE(all, 360);
}

TEST_F(ModuleComponentTest, TheBackArrowReturnsToTheSources) {
    NoMotion motion;
    AddFixture f;
    f.openAdd();
    clickNow(f.add().backButton());
    EXPECT_EQ(f.panel->page(), ModDotPopover::Page::Sources);
}

TEST_F(ModuleComponentTest, TheAddSourceListIsTheModMatrixsSourceList) {
    NoMotion motion;
    AddFixture f;
    auto& graph = f.engine.getGraph();
    graph.addNode(std::make_unique<FilterModule>());
    ModMatrixComponent matrix(f.engine);
    matrix.setSize(600, 400);
    f.engine.addEmptyModRouting();
    matrix.updateRowsFromGraph();
    auto* combo = matrix.getRowSourceComboForTest(0);
    ASSERT_NE(combo, nullptr);

    std::vector<std::pair<int, juce::String>> matrixItems, catalogItems;
    for (int i = 0; i < combo->getNumItems(); ++i)
        matrixItems.emplace_back(combo->getItemId(i), combo->getItemText(i));
    for (const auto& item : synth::ui::enumerateModSources(graph))
        catalogItems.emplace_back(item.itemId(), item.label());
    ASSERT_FALSE(catalogItems.empty());
    EXPECT_EQ(matrixItems, catalogItems);
}
