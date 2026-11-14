// OnCardEditorGroupTests.cpp -- a hidden control shown again returns to its own group, and a control dragged
// onto another group moves into it (one undo step), through the real panel rows and mouse handlers.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "OnCardTestHelpers.h"
#include "UI/Graph/CardLayoutEditor/OnCard/OnCardCells.h"
#include "UI/Graph/CardLayoutEditor/OnCard/OnCardLayoutMath.h"
#include "UI/Library/ModuleLibraryComponent/ModuleLibraryComponent.h"

using namespace oncard_test;

namespace {

constexpr int kCommand = juce::ModifierKeys::commandModifier;

/** The id of the layout section that lists `paramId`, or empty. */
juce::String sectionIdOf(const synth::CardLayout& layout, const juce::String& paramId) {
    for (const auto& section : layout.sections)
        for (const auto& item : section.items)
            if (const auto* param = std::get_if<synth::CardParamItem>(&item);
                param != nullptr && param->paramId == paramId)
                return section.id;
    return {};
}

struct Oscillator {
    OnCardRig rig;
    NodeID id = rig.add(std::make_unique<OscillatorModule>(), 100, 100);
    CardLayoutOnCardEditor* editor = rig.openOnCard(id);
};

} // namespace

// Regression test for FRO706: Add control put a hidden Detune in the Output group.
TEST(OnCardEditorGroups, ADetuneHiddenAndAddedBackIsInTheUnisonGroupAgain) {
    Oscillator osc;
    ASSERT_NE(osc.editor, nullptr);
    hideThroughPanel(osc.rig, *osc.editor, "detune");
    ASSERT_EQ(osc.editor->getOutlineForTest("detune"), nullptr);

    auto* panel = openAddPanel(osc.rig, *osc.editor);
    ASSERT_NE(panel, nullptr);
    clickRow(*panel->getRowForTest("detune"));

    const auto layout = osc.rig.storedLayout(osc.id);
    ASSERT_TRUE(layout.has_value());
    EXPECT_FALSE(layout->hidden.contains("detune"));
    EXPECT_EQ(sectionIdOf(*layout, "detune"), "unison");
    ASSERT_NE(osc.editor->getOutlineForTest("detune"), nullptr);
    EXPECT_EQ(osc.editor->getCellRectForTest("detune").getY(), osc.editor->getCellRectForTest("unison").getY())
        << "on the Unison row";
}

TEST(OnCardEditorGroups, DraggingLevelFromOutputOntoUnisonMovesItIntoThatGroupAsOneUndoStep) {
    Oscillator osc;
    ASSERT_NE(osc.editor, nullptr);
    const auto level = osc.editor->getCellRectForTest("level");
    const auto unison = osc.editor->getCellRectForTest("unison");
    const auto detune = osc.editor->getCellRectForTest("detune");
    ASSERT_LT(unison.getY(), level.getY());
    const int serial = osc.rig.canvas.undo.getEditSerial();

    Pointer pointer(*osc.editor, "level");
    pointer.moveBy({detune.getRight() + 20 - level.getCentreX(), unison.getCentreY() - level.getCentreY()}, kCommand);
    EXPECT_TRUE(osc.editor->isDraggingForTest());
    pointer.release();

    const auto layout = osc.rig.storedLayout(osc.id);
    ASSERT_TRUE(layout.has_value());
    EXPECT_EQ(sectionIdOf(*layout, "level"), "unison") << "now an item of the Unison group";
    EXPECT_NE(sectionIdOf(*layout, "pan"), "unison") << "Pan stayed in Output";
    const auto* item = storedItem(*layout, "level");
    ASSERT_NE(item, nullptr);
    EXPECT_TRUE(item->at.has_value()) << "at the drop position";
    EXPECT_EQ(osc.rig.canvas.undo.getEditSerial(), serial + 1) << "one write";
    EXPECT_EQ(osc.editor->getLastAnnouncementForTest(), "Level moved to Unison");
    const auto landed = osc.editor->getCellRectForTest("level");
    for (const auto* other : {"unison", "detune"})
        EXPECT_FALSE(landed.intersects(osc.editor->getCellRectForTest(other)))
            << other; // flush is fine, overlap is not

    ASSERT_TRUE(osc.rig.canvas.undo.undo());
    osc.editor->runQueuedSyncForTest();
    EXPECT_FALSE(osc.rig.storedLayout(osc.id).has_value()) << "undo puts everything back";
    EXPECT_EQ(osc.editor->getCellRectForTest("level"), level);
}

// ---- A control dropped on another group's row lands where it was aimed (Filter's large Cutoff) ----

namespace {

struct FilterOpen {
    OnCardRig rig;
    NodeID id = rig.add(std::make_unique<FilterModule>(), 200, 200);
    CardLayoutOnCardEditor* editor = rig.openOnCard(id);

    juce::Rectangle<int> cell(const juce::String& key) const { return editor->getCellRectForTest(key); }

    // Moves `key` with the pointer ending at `target` (card pixels); the drag is left unreleased in the returned
    // pointer.
    std::unique_ptr<Pointer> dragTo(const juce::String& key, juce::Point<int> target) {
        auto pointer = std::make_unique<Pointer>(*editor, key);
        pointer->moveBy(target - cell(key).getCentre(), kCommand);
        return pointer;
    }

    bool sameRow(const juce::String& a, const juce::String& b) const {
        return cell(a).getY() < cell(b).getBottom() && cell(b).getY() < cell(a).getBottom();
    }

    void expectInsideCard(const std::vector<juce::String>& keys) {
        const auto g = synth::cardbody::BodyGeometry::forCardWidth(rig.card(id)->getWidth());
        for (size_t i = 0; i < keys.size(); ++i) {
            EXPECT_GE(cell(keys[i]).getX(), g.contentX) << keys[i];
            EXPECT_LE(cell(keys[i]).getRight(), g.contentX + g.contentW) << keys[i];
            for (size_t j = i + 1; j < keys.size(); ++j)
                EXPECT_FALSE(cell(keys[i]).intersects(cell(keys[j]))) << keys[i] << " and " << keys[j];
        }
    }
};

} // namespace

// Regression test for FRO1761: Cutoff dropped onto the Level row ended on a row of its own below it.
TEST(OnCardEditorCrossGroupDrop, CutoffDroppedOnTheLevelRowJoinsThatRow) {
    FilterOpen open;
    ASSERT_NE(open.editor, nullptr);
    const auto level = open.cell("outputLevel");
    for (const int dx : {-30, 20, 60}) {
        FilterOpen fresh;
        auto pointer = fresh.dragTo("cutoff", {fresh.cell("outputLevel").getCentreX() + dx, level.getCentreY()});
        pointer->release();
        EXPECT_TRUE(fresh.sameRow("cutoff", "outputLevel")) << "pointer " << dx << " from Level's centre";
        EXPECT_TRUE(fresh.sameRow("cutoff", "keyTrack")) << dx;
        fresh.expectInsideCard({"cutoff", "outputLevel", "keyTrack"});
    }
}

TEST(OnCardEditorCrossGroupDrop, CutoffDroppedOnThePanRowJoinsThatRow) {
    for (const int dx : {-30, 20, 60}) {
        FilterOpen open;
        ASSERT_NE(open.editor, nullptr);
        auto pointer = open.dragTo("cutoff", {open.cell("pan").getCentreX() + dx, open.cell("pan").getCentreY()});
        pointer->release();
        EXPECT_TRUE(open.sameRow("cutoff", "pan")) << dx;
        EXPECT_TRUE(open.sameRow("cutoff", "mix")) << dx;
        open.expectInsideCard({"cutoff", "pan", "mix"});
    }
}

TEST(OnCardEditorCrossGroupDrop, CutoffStillJoinsResonancesRowAndDraggedBackLandsWhereItBegan) {
    FilterOpen open;
    ASSERT_NE(open.editor, nullptr);
    const auto start = open.cell("cutoff");
    open.dragTo("cutoff", open.cell("resonance").getCentre())->release();
    EXPECT_TRUE(open.sameRow("cutoff", "resonance")) << "its own group's row, as before";
    EXPECT_TRUE(open.sameRow("cutoff", "drive"));
    open.expectInsideCard({"cutoff", "resonance", "drive"});

    open.dragTo("cutoff", open.cell("outputLevel").getCentre())->release();
    ASSERT_TRUE(open.sameRow("cutoff", "outputLevel"));
    open.dragTo("cutoff", start.getCentre())->release();
    EXPECT_EQ(open.cell("cutoff").getPosition(), start.getPosition()) << "back on the row it began on";
}

// A drag to another group shows the dropped control and the target's row as the commit leaves them; the group it
// left keeps its hole until the release (the cards below do not move under a drag), so only the target's cells are
// compared. One undo step puts everything back.
TEST(OnCardEditorCrossGroupDrop, ThePreviewIsTheCommitInTheTargetRowAndOneUndoPutsEverythingBack) {
    FilterOpen open;
    ASSERT_NE(open.editor, nullptr);
    std::map<juce::String, juce::Rectangle<int>> before;
    const std::vector<juce::String> keys{"cutoff", "resonance", "drive", "keyTrack", "outputLevel", "mix", "pan"};
    for (const auto& k : keys)
        before[k] = open.cell(k);

    auto pointer = open.dragTo("cutoff", open.cell("outputLevel").getCentre());
    std::map<juce::String, juce::Rectangle<int>> preview;
    for (const auto& k : keys)
        preview[k] = open.cell(k);
    pointer->release();
    for (const auto* k : {"keyTrack", "outputLevel"}) {
        EXPECT_EQ(open.cell("cutoff").getX() - open.cell(k).getX(), preview["cutoff"].getX() - preview[k].getX()) << k;
        EXPECT_EQ(open.cell("cutoff").getY() - open.cell(k).getY(), preview["cutoff"].getY() - preview[k].getY()) << k;
    }

    ASSERT_TRUE(open.rig.canvas.undo.undo());
    open.editor->runQueuedSyncForTest();
    for (const auto& k : keys)
        EXPECT_EQ(open.cell(k), before[k]) << k << " after one undo";
}

TEST(OnCardEditorCrossGroupDrop, EscapeMidDragPutsEverythingBack) {
    FilterOpen open;
    ASSERT_NE(open.editor, nullptr);
    std::map<juce::String, juce::Rectangle<int>> before;
    const std::vector<juce::String> keys{"cutoff", "resonance", "drive", "keyTrack", "outputLevel", "mix", "pan"};
    for (const auto& k : keys)
        before[k] = open.cell(k);
    auto pointer = open.dragTo("cutoff", open.cell("outputLevel").getCentre());
    ASSERT_TRUE(open.editor->sendEscapeToDragForTest());
    for (const auto& k : keys)
        EXPECT_EQ(open.cell(k), before[k]) << k;
}

// land-off-aim guard: on every built-in card, a control of every group (and the card's tallest one) dropped on the
// first row of every other group lands on that row whenever the row's boxes and the dropped one fit the content width
// side by side, however the group laid its cells out. It never silently starts a row of its own while the width allows.
TEST(OnCardEditorCrossGroupDrop, EveryBuiltInCardsControlDroppedOnAnotherGroupsFirstRowJoinsItWhereTheWidthAllows) {
    ModuleLibraryComponent library;
    auto types = library.getDraggableModuleNames();
    for (const char* extra : {"Amp Env", "Filter Env"})
        types.addIfNotAlreadyThere(extra);
    int dropsChecked = 0;
    for (const auto& type : types) {
        auto processor = synth::AIStateMapper::createModule(type);
        if (processor == nullptr)
            continue;
        OnCardRig rig;
        const auto id = rig.add(std::move(processor), 200, 200);
        const auto* body = rig.card(id) != nullptr ? rig.card(id)->getCardBody() : nullptr;
        if (body == nullptr || !body->drawsFromLayout())
            continue;
        auto* editor = rig.openOnCard(id);
        if (editor == nullptr)
            continue;
        const auto g = rig.card(id)->bodyGeometry();
        const auto cells = synth::ui::collectCells(*rig.card(id));
        std::vector<size_t> movers;
        std::map<int, size_t> firstOfSection;
        size_t tallest = cells.size();
        for (size_t i = 0; i < cells.size(); ++i) {
            if (cells[i].panelOnly)
                continue;
            firstOfSection.emplace(cells[i].section, i);
            if (tallest == cells.size() || cells[i].rect.getHeight() > cells[tallest].rect.getHeight())
                tallest = i;
        }
        for (const auto& entry : firstOfSection)
            movers.push_back(entry.second);
        if (tallest < cells.size() && std::find(movers.begin(), movers.end(), tallest) == movers.end())
            movers.push_back(tallest);

        for (const size_t m : movers)
            for (const auto& entry : firstOfSection) {
                const int target = entry.first;
                if (target == cells[m].section)
                    continue;
                // The target's first row: its cells that share the topmost cell's vertical extent.
                std::vector<size_t> row;
                for (size_t i = 0; i < cells.size(); ++i)
                    if (!cells[i].panelOnly && cells[i].section == target)
                        row.push_back(i);
                std::sort(row.begin(), row.end(),
                          [&](size_t a, size_t b) { return cells[a].rect.getY() < cells[b].rect.getY(); });
                const auto top = cells[row.front()].rect;
                std::erase_if(row, [&](size_t i) {
                    return !cells[i].rect.getUnion(top).isEmpty() && cells[i].rect.getY() >= top.getBottom();
                });
                int rowRight = 0;
                int rowWidth = 0;
                for (size_t i : row) {
                    rowRight = std::max(rowRight, cells[i].rect.getRight());
                    rowWidth += cells[i].rect.getWidth();
                }

                Pointer pointer(*editor, cells[m].key);
                pointer.moveBy({std::min(rowRight + 5, g.contentX + g.contentW - 1) - cells[m].rect.getCentreX(),
                                top.getCentreY() - cells[m].rect.getCentreY()},
                               kCommand);
                pointer.release();
                ++dropsChecked;

                const auto landed = editor->getCellRectForTest(cells[m].key);
                const bool fits = rowWidth + landed.getWidth() <= g.contentW;
                bool joined = false;
                for (size_t i : row) {
                    const auto mate = editor->getCellRectForTest(cells[i].key);
                    joined = joined || (landed.getY() < mate.getBottom() && mate.getY() < landed.getBottom());
                }
                if (fits)
                    EXPECT_TRUE(joined) << type << ": " << cells[m].key << " onto the row of "
                                        << cells[row.front()].key;
                rig.canvas.undo.undo();
                editor->runQueuedSyncForTest();
            }
    }
    EXPECT_GT(dropsChecked, 100) << "the walk reached the cards' groups";
}
