// The mod dot's panel, inline Add source: the list unfolds right under the rows in the same panel (no page switch),
// the rows say how many things each source moves, a typed query offers "New <module>" rows that create the module and
// connect it in one undo step, and the split button's halves, keys and accessible names. Driven by real mouse and
// key events on the real controls. docs/modules/modulation.md#the-mod-dot-menu.

#include "ModDotTestFixture.h"

#include "AudioEngine/ModuleTitle.h"
#include "Modules/ADSRModule.h"
#include "UI/Graph/ModDot/ModSourceCatalog.h"
#include "UI/Layout/ReducedMotion.h"

namespace {

using synth::ui::ModDotAddSourcePage;
using synth::ui::ModDotChoiceRow;
using synth::ui::ModDotPopover;

struct NoMotion {
    NoMotion() { synth::ui::setReducedMotionForTest(true); }
    ~NoMotion() { synth::ui::setReducedMotionForTest(std::nullopt); }
};

void typeInto(juce::TextEditor& editor, const juce::String& text) {
    for (auto c : text)
        editor.keyPressed(juce::KeyPress((int)c, juce::ModifierKeys(), c));
    editor.onTextChange();
}

// A press and release on a button as the mouse delivers them (its click fires on the release).
void mouseClick(juce::Component& c) {
    const auto at = c.getLocalBounds().getCentre();
    c.mouseDown(makeModuleClickWithMods(c, at, kPlain));
    c.mouseUp(makeModuleClickWithMods(c, at, kPlain));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
}

// The fixture (LFO -> VCA gain) plus an LFO nobody uses and an envelope, with the panel open on the dot.
struct InlineFixture : Fixture {
    juce::AudioProcessorGraph::NodeID lfo2, adsr;
    ModDotPopover* panel = nullptr;

    InlineFixture() {
        auto& graph = engine.getGraph();
        lfo2 = graph.addNode(std::make_unique<LFOModule>())->nodeID;
        adsr = graph.addNode(std::make_unique<ADSRModule>())->nodeID;
        refresh();
        panel = clickDot();
    }

    ModDotAddSourcePage& list() { return panel->addSourcePage(); }
    juce::String titleOf(juce::AudioProcessorGraph::NodeID id) {
        return synth::moduleTitle(*engine.getGraph().getNodeForId(id));
    }
    ModDotChoiceRow* rowLabelled(const juce::String& label) {
        for (int i = 0; auto* row = list().visibleRow(i); ++i)
            if (row->item().label() == label)
                return row;
        return nullptr;
    }
    void openList() { clickNow(panel->sourcesPage().splitButton().leftHalf()); }
};

} // namespace

TEST_F(ModuleComponentTest, TheListUnfoldsRightUnderTheRowsInTheSamePanel) {
    NoMotion motion;
    InlineFixture f;
    auto& rows = f.panel->sourcesPage();
    const int foldedHeight = f.panel->getHeight();
    ASSERT_EQ(foldedHeight, rows.preferredHeight());
    ASSERT_FALSE(f.list().isVisible());

    f.openList();

    EXPECT_TRUE(f.panel->isListOpen());
    EXPECT_TRUE(f.list().isVisible());
    EXPECT_TRUE(rows.isVisible()) << "the rows stay visible above the list";
    ASSERT_NE(rows.rowAt(0), nullptr);
    EXPECT_TRUE(rows.rowAt(0)->isVisible());
    EXPECT_EQ(f.list().getY(), rows.getBottom()) << "the list starts right under the rows";
    EXPECT_GT(f.panel->getHeight(), foldedHeight);
    EXPECT_EQ(f.panel->getHeight(), f.panel->settledHeight(true));
    EXPECT_EQ(rows.getHeight(), foldedHeight) << "the rows page is not resized by the list";

    mouseClick(rows.splitButton().leftHalf());
    EXPECT_FALSE(f.panel->isListOpen()) << "a second click folds it back";
    EXPECT_EQ(f.panel->getHeight(), foldedHeight);
    EXPECT_FALSE(f.list().isVisible());
}

TEST_F(ModuleComponentTest, EscapeFoldsTheListAndTheNextEscapeClosesThePanel) {
    NoMotion motion;
    InlineFixture f;
    int closed = 0;
    f.panel->onDismiss = [&] { ++closed; };
    f.openList();
    ASSERT_TRUE(f.panel->isListOpen());

    EXPECT_TRUE(f.panel->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_FALSE(f.panel->isListOpen());
    EXPECT_EQ(closed, 0);
    EXPECT_TRUE(f.panel->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_EQ(closed, 1);
}

TEST_F(ModuleComponentTest, SourceRowsSayHowManyTargetsTheySourceMovesOrNotUsedYet) {
    NoMotion motion;
    InlineFixture f;
    f.openList();
    ASSERT_NE(f.rowLabelled(f.titleOf(f.lfoId)), nullptr);
    EXPECT_EQ(f.rowLabelled(f.titleOf(f.lfoId))->usageText(), "1 target");
    EXPECT_EQ(f.rowLabelled(f.titleOf(f.lfo2))->usageText(), "Not used yet");
    EXPECT_NE(f.rowLabelled(f.titleOf(f.lfo2))->getTitle().indexOf("Not used yet"), -1) << "read out too";

    // The first LFO also drives another module's gain: two targets.
    auto vca2 = f.engine.getGraph().addNode(std::make_unique<VCAModule>());
    f.engine.addModRouting(f.lfoId, 0, vca2->nodeID, f.gainChannel);
    f.refresh();
    f.panel->syncFromGraph();
    EXPECT_EQ(f.rowLabelled(f.titleOf(f.lfoId))->usageText(), "2 targets") << "the open list follows the graph";
}

TEST(ModDotUsageText, WordsTheCount) {
    EXPECT_EQ(synth::ui::modSourceUsageText(0), "Not used yet");
    EXPECT_EQ(synth::ui::modSourceUsageText(1), "1 target");
    EXPECT_EQ(synth::ui::modSourceUsageText(3), "3 targets");
}

TEST_F(ModuleComponentTest, ATypedQueryOffersNewModuleRowsAndAnEmptyOneDoesNot) {
    NoMotion motion;
    InlineFixture f;
    f.openList();
    for (const auto& label : f.list().visibleRowLabels())
        EXPECT_FALSE(label.startsWith("New ")) << "no query, no New rows";

    typeInto(f.list().searchEditor(), "adsr");

    auto* row = f.rowLabelled("New Env");
    ASSERT_NE(row, nullptr);
    EXPECT_TRUE(row->isNew());
    EXPECT_EQ(row->newType(), "ADSR");
    EXPECT_EQ(row->getTitle(), "New Env, new module");
    EXPECT_NE(f.list().header("New module"), nullptr);
    EXPECT_NE(f.rowLabelled(f.titleOf(f.adsr)), nullptr) << "the existing envelope is still offered";
}

TEST_F(ModuleComponentTest, NewModuleListOffersOneEnvelopeEntryWhoseAliasesKeepTheOtherKeysFindable) {
    int envelopes = 0;
    for (const auto& source : synth::ui::newModuleSources())
        if (source.label == "Env") {
            ++envelopes;
            EXPECT_EQ(source.typeName, "ADSR") << "the first factory key stands for the class";
            for (const char* word : {"envelope", "Amp Env", "Filter Env"})
                EXPECT_TRUE(source.aliases.containsIgnoreCase(word)) << word;
        }
    EXPECT_EQ(envelopes, 1);
    for (const auto& source : synth::ui::newModuleSources()) {
        EXPECT_FALSE(source.typeName == "Amp Env") << "the twin keys are not rows of their own";
        EXPECT_FALSE(source.typeName == "Filter Env");
    }

    NoMotion motion;
    InlineFixture f;
    f.openList();
    typeInto(f.list().searchEditor(), "filter env");
    EXPECT_NE(f.rowLabelled("New Env"), nullptr);
    EXPECT_EQ(f.rowLabelled("New Filter Env"), nullptr);
}

TEST_F(ModuleComponentTest, ChoosingNewModuleCreatesItBesideTheCardAndConnectsItInOneUndoStep) {
    NoMotion motion;
    InlineFixture f;
    f.openList();
    typeInto(f.list().searchEditor(), "adsr");
    auto* row = f.rowLabelled("New Env");
    ASSERT_NE(row, nullptr);
    const auto nodesBefore = f.engine.getGraph().getNumNodes();
    const auto before = f.engine.getGraph().getNodes();
    ASSERT_FALSE(f.undo.canUndo());

    row->mouseUp(makeModuleClickWithMods(*row, {10, 5}, kPlain));

    EXPECT_EQ(f.engine.getModulationRoutings().size(), 2u) << "connected to the knob";
    EXPECT_EQ(f.engine.getGraph().getNumNodes(), nodesBefore + 2) << "the module and its hidden attenuverter";
    auto& sources = f.panel->sourcesPage();
    ASSERT_EQ(sources.rowCount(), 2);
    auto* added = sources.rowAt(1);
    EXPECT_TRUE(added->isSelected());
    EXPECT_NEAR(f.amount(added->attenuverterId()), synth::ui::kModDotNewSourceDepth, 1e-4f);
    EXPECT_FALSE(f.panel->isListOpen()) << "the list folds after the pick";

    f.refresh();
    juce::AudioProcessorGraph::Node::Ptr created;
    for (auto* node : f.engine.getGraph().getNodes()) {
        const bool isNew = std::none_of(before.begin(), before.end(), [node](auto* n) { return n == node; });
        if (isNew && dynamic_cast<ADSRModule*>(node->getProcessor()) != nullptr)
            created = node;
    }
    ASSERT_NE(created, nullptr);
    auto* card = findModuleComp(*f.editor, created->getProcessor());
    ASSERT_NE(card, nullptr);
    EXPECT_FALSE(card->getBounds().intersects(f.vcaCard->getBounds())) << "placed beside the card, not on it";

    ASSERT_TRUE(f.undo.undo());
    EXPECT_EQ(f.engine.getGraph().getNumNodes(), nodesBefore) << "one undo removes the module and the cable";
    EXPECT_EQ(f.engine.getModulationRoutings().size(), 1u);
    EXPECT_FALSE(f.undo.canUndo()) << "the whole thing was one undo step";
}

TEST_F(ModuleComponentTest, ReturnPicksAnExistingMatchBeforeANewModule) {
    NoMotion motion;
    InlineFixture f;
    f.openList();
    typeInto(f.list().searchEditor(), "adsr");
    const auto nodesBefore = f.engine.getGraph().getNumNodes();

    EXPECT_TRUE(f.list().searchEditor().keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));

    EXPECT_EQ(f.engine.getGraph().getNumNodes(), nodesBefore + 1) << "only the attenuverter: no module was created";
    EXPECT_EQ(f.panel->sourcesPage().rowAt(1)->source().sourceNodeId, f.adsr);
}

TEST_F(ModuleComponentTest, ReturnWithOnlyANewModuleMatchCreatesIt) {
    NoMotion motion;
    InlineFixture f;
    f.openList();
    // A creatable type nothing in the patch matches (the patch has an LFO, an envelope and a VCA).
    juce::String type;
    for (const auto& candidate : synth::ui::newModuleSources())
        if (!candidate.typeName.containsIgnoreCase("lfo") && !candidate.typeName.containsIgnoreCase("env") &&
            !candidate.typeName.containsIgnoreCase("adsr") && !candidate.typeName.containsIgnoreCase("vca")) {
            type = candidate.typeName;
            break;
        }
    ASSERT_TRUE(type.isNotEmpty());
    typeInto(f.list().searchEditor(), type);
    ASSERT_NE(f.rowLabelled("New " + type), nullptr);
    const auto nodesBefore = f.engine.getGraph().getNumNodes();

    EXPECT_TRUE(f.list().searchEditor().keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));

    EXPECT_GE(f.engine.getGraph().getNumNodes(), nodesBefore + 2);
    EXPECT_EQ(f.panel->sourcesPage().rowCount(), 2);
}

TEST_F(ModuleComponentTest, TheSplitButtonsHalvesAreSeparateControlsThatLightWithTheirMode) {
    NoMotion motion;
    InlineFixture f;
    auto& split = f.panel->sourcesPage().splitButton();
    EXPECT_EQ(split.leftHalf().getTitle(), "Add source");
    EXPECT_EQ(split.leftHalf().getTooltip(), "Add source from a list");
    EXPECT_EQ(split.rightHalf().getTitle(), "Pick on canvas");
    EXPECT_FALSE(split.rightHalf().getTooltip().isEmpty());
    EXPECT_TRUE(split.leftHalf().getWantsKeyboardFocus());
    EXPECT_TRUE(split.rightHalf().getWantsKeyboardFocus());
    EXPECT_LT(split.leftHalf().getRight(), split.rightHalf().getRight());
    EXPECT_EQ(split.leftHalf().getRight(), split.rightHalf().getX()) << "two halves, side by side";

    mouseClick(split.leftHalf());
    EXPECT_TRUE(split.isLeftLit());
    EXPECT_FALSE(split.isRightLit()) << "the list half does not start a pick";
    EXPECT_EQ(split.leftHalf().getTitle(), "Add source, list open");

    mouseClick(split.rightHalf());
    EXPECT_TRUE(split.isRightLit());
    EXPECT_TRUE(f.panel->isListOpen()) << "the pick half leaves the list as it was";
    EXPECT_EQ(split.rightHalf().getTitle(), "Pick on canvas, on");
}

// juce::Button keeps keyPressed protected; the public Component entry point is what a key event reaches.
static bool pressKey(juce::Component& target, const juce::KeyPress& key) { return target.keyPressed(key); }

TEST_F(ModuleComponentTest, TheListIsReachableAndOperableFromTheKeyboardAlone) {
    NoMotion motion;
    InlineFixture f;
    auto& split = f.panel->sourcesPage().splitButton();
    EXPECT_TRUE(pressKey(split.leftHalf(), juce::KeyPress(juce::KeyPress::returnKey)));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
    ASSERT_TRUE(f.panel->isListOpen()) << "Return on the list half opens the list";

    EXPECT_TRUE(pressKey(split.leftHalf(), juce::KeyPress(juce::KeyPress::rightKey))) << "Right hops to the next half";
    EXPECT_TRUE(f.panel->sourcesPage().keyPressed(juce::KeyPress(juce::KeyPress::downKey)));
    EXPECT_TRUE(f.list().keyPressed(juce::KeyPress(juce::KeyPress::downKey)));

    auto* row = f.rowLabelled(f.titleOf(f.lfo2));
    ASSERT_NE(row, nullptr);
    EXPECT_TRUE(row->getWantsKeyboardFocus());
    EXPECT_TRUE(row->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
    EXPECT_EQ(f.engine.getModulationRoutings().size(), 2u) << "Return on a row adds it";

    EXPECT_TRUE(pressKey(split.rightHalf(), juce::KeyPress(juce::KeyPress::leftKey))) << "Left hops back";
    EXPECT_TRUE(pressKey(split.rightHalf(), juce::KeyPress(juce::KeyPress::spaceKey)));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
    EXPECT_TRUE(f.panel->isPicking()) << "Space on the pick half starts the pick";
}

TEST_F(ModuleComponentTest, TheListsControlsAreNamedForAScreenReader) {
    NoMotion motion;
    InlineFixture f;
    f.openList();
    EXPECT_EQ(f.list().searchEditor().getTitle(), "Search sources");
    EXPECT_EQ(f.list().expandAllButton().getTitle(), "Expand all groups");
    EXPECT_EQ(f.list().collapseAllButton().getTitle(), "Collapse all groups");
    ASSERT_NE(f.list().header("LFOs"), nullptr);
    EXPECT_EQ(f.list().header("LFOs")->getTitle(), "LFOs, expanded");
    EXPECT_FALSE(f.list().getTitle().isEmpty());
}

TEST_F(ModuleComponentTest, AClickOnTheDotAgainClosesThePanel) {
    NoMotion motion;
    Fixture f;
    auto* panel = f.clickDot();
    ASSERT_NE(panel, nullptr);
    int closed = 0;
    panel->onDismiss = [&] { ++closed; };

    f.clickDot();

    EXPECT_EQ(closed, 1);
    EXPECT_EQ(f.editor->getModDot().getPopover(), nullptr);
}
