// OnCardGroupMoveEditorTests.cpp -- moving a whole group in Edit Layout through the real handlers: the title row's
// mouse events and keys on the oscillator card. The drop is ONE undo step, the controls keep their place in their
// group, nothing overlaps afterwards, and what the drag showed is what the drop wrote.

#include "../../../Accessibility/AccessibilityAudit.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/OscillatorModule.h"
#include "OnCardTestHelpers.h"
#include "UI/Graph/CardLayoutEditor/OnCard/OnCardCells.h"
#include "UI/Layout/DragCursor.h"
#include <map>

using namespace oncard_test;

namespace {

constexpr int kAlt = juce::ModifierKeys::altModifier;

struct Oscillator {
    OnCardRig rig;
    NodeID id = rig.add(std::make_unique<OscillatorModule>(), 100, 100);
    CardLayoutOnCardEditor* editor = rig.openOnCard(id);

    ModuleComponent& card() { return *rig.card(id); }
    juce::String announced() const { return editor->getLastAnnouncementForTest(); }
    int serial() { return rig.canvas.undo.getEditSerial(); }
};

/** Every outlined control's rect on the card now, by parameter id. */
std::map<juce::String, juce::Rectangle<int>> cellRects(ModuleComponent& card) {
    std::map<juce::String, juce::Rectangle<int>> rects;
    for (const auto& cell : synth::ui::collectCells(card))
        rects[cell.key] = cell.rect;
    return rects;
}

/** The section id of every plan section with a title row, to its block now. */
std::map<juce::String, juce::Rectangle<int>> blocksOf(CardLayoutOnCardEditor& editor, const synth::CardBodyPlan& plan) {
    std::map<juce::String, juce::Rectangle<int>> blocks;
    for (const auto& section : plan.sections)
        if (section.hasHeader())
            blocks[section.id] = editor.getGroupBlockForTest(section.id);
    return blocks;
}

/** The controls the plan lists in `sectionId`. */
std::vector<juce::String> controlsOf(const synth::CardBodyPlan& plan, const juce::String& sectionId) {
    std::vector<juce::String> ids;
    for (const auto& section : plan.sections)
        if (section.id == sectionId)
            for (int item : section.items)
                if (plan.items[(size_t)item].param != nullptr)
                    ids.push_back(plan.items[(size_t)item].param->paramID);
    return ids;
}

/** Fails when two controls overlap, or two groups' blocks do. */
void expectNothingOverlaps(Oscillator& osc, const juce::String& when) {
    const auto rects = cellRects(osc.card());
    for (auto a = rects.begin(); a != rects.end(); ++a)
        for (auto b = std::next(a); b != rects.end(); ++b)
            EXPECT_FALSE(a->second.intersects(b->second)) << when << ": " << a->first << " over " << b->first;
    const auto blocks = blocksOf(*osc.editor, osc.card().getCardBody()->getPlan());
    for (auto a = blocks.begin(); a != blocks.end(); ++a)
        for (auto b = std::next(a); b != blocks.end(); ++b)
            EXPECT_FALSE(a->second.intersects(b->second)) << when << ": group " << a->first << " over " << b->first;
}

bool undoWithEditorOpen(Oscillator& osc, const juce::String& focusedGroup) {
    const juce::KeyPress key('z', juce::ModifierKeys(juce::ModifierKeys::commandModifier), 0);
    EXPECT_FALSE(osc.editor->getGroupHandleForTest(focusedGroup)->keyPressed(key)) << "left for the app";
    const bool did = osc.rig.canvas.undo.undo();
    osc.editor->runQueuedSyncForTest();
    return did;
}

bool redoWithEditorOpen(Oscillator& osc, const juce::String& focusedGroup) {
    const juce::KeyPress key(
        'z', juce::ModifierKeys(juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier), 0);
    EXPECT_FALSE(osc.editor->getGroupHandleForTest(focusedGroup)->keyPressed(key)) << "left for the app";
    const bool did = osc.rig.canvas.undo.redo();
    osc.editor->runQueuedSyncForTest();
    return did;
}

/** The pointer travel that carries the Unison title row to just past Output's middle. */
juce::Point<int> unisonBelowOutput(Oscillator& osc) {
    const auto unison = osc.editor->getGroupBlockForTest("unison");
    const auto output = osc.editor->getGroupBlockForTest("output");
    return {0, output.getBottom() - unison.getBottom()};
}

} // namespace

TEST(OnCardGroupMoveEditor, EveryTitledGroupHasAHandleNamedForItAndTheEditorAuditsClean) {
    Oscillator osc;
    ASSERT_NE(osc.editor, nullptr);
    for (const auto* id : {"pitch", "unison", "output"}) {
        auto* handle = osc.editor->getGroupHandleForTest(id);
        ASSERT_NE(handle, nullptr) << id;
        EXPECT_TRUE(handle->getWantsKeyboardFocus()) << id;
        EXPECT_FALSE(handle->getTooltip().isEmpty()) << id;
    }
    EXPECT_EQ(osc.editor->getGroupHandleForTest("unison")->getTitle(), "Unison group");
    EXPECT_EQ(osc.editor->getGroupHandleForTest("output")->getTitle(), "Output group");
    EXPECT_EQ(osc.editor->getGroupHandleForTest("waveform"), nullptr) << "an untitled group has no title row to grab";
    EXPECT_EQ(osc.editor->getGroupHandleForTest("footer"), nullptr);
    const auto gaps = synth::test::auditAccessibility(*osc.editor);
    EXPECT_TRUE(gaps.empty()) << "gaps: " << gaps.size();
}

// Only the title row takes the mouse, so the controls inside the group keep their own outlines; hovering the title
// shows the grab cursor and a faint outline round the whole group, and nothing at rest.
TEST(OnCardGroupMoveEditor, OnlyTheTitleRowTakesTheMouseAndHoverOutlinesTheWholeGroup) {
    Oscillator osc;
    ASSERT_NE(osc.editor, nullptr);
    auto* handle = osc.editor->getGroupHandleForTest("unison");
    ASSERT_NE(handle, nullptr);
    const auto block = osc.editor->getGroupBlockForTest("unison");
    EXPECT_EQ(handle->getBounds().getY(), block.getY());
    EXPECT_EQ(handle->getHeight(), block.getHeight()) << "spans the group, so the outline can surround it";
    EXPECT_TRUE(handle->hitTest(handle->getWidth() / 2, 2));
    const auto control = osc.editor->getCellRectForTest("detune").getCentre() - handle->getPosition();
    EXPECT_FALSE(handle->hitTest(control.x, control.y)) << "a control's cell stays the control's";
    EXPECT_EQ(handle->getMouseCursor(), synth::ui::dragGrabCursor());

    const auto paintedAtEdge = [&] {
        juce::Image image(juce::Image::ARGB, handle->getWidth(), handle->getHeight(), true);
        juce::Graphics g(image);
        handle->paintEntireComponent(g, true);
        return image.getPixelAt(handle->getWidth() / 2, handle->getHeight() - 2).getAlpha();
    };
    EXPECT_EQ(paintedAtEdge(), 0) << "nothing drawn at rest";
    handle->mouseEnter(cardbody_test::mouseAt(*handle, {5.0f, 5.0f}, juce::ModifierKeys()));
    EXPECT_GT(paintedAtEdge(), 0) << "a faint outline round the group's far edge";
    handle->mouseExit(cardbody_test::mouseAt(*handle, {5.0f, 5.0f}, juce::ModifierKeys()));
    EXPECT_EQ(paintedAtEdge(), 0);
}

TEST(OnCardGroupMoveEditor, DraggingUnisonsTitleBelowOutputMovesTheWholeGroupAsOneUndoStep) {
    Oscillator osc;
    ASSERT_NE(osc.editor, nullptr);
    const auto cellsBefore = cellRects(osc.card());
    const auto& plan = osc.card().getCardBody()->getPlan();
    const auto blocksBefore = blocksOf(*osc.editor, plan);
    const int heightBefore = osc.card().getHeight();
    const auto unisonControls = controlsOf(plan, "unison");
    const auto outputControls = controlsOf(plan, "output");
    ASSERT_FALSE(unisonControls.empty());
    ASSERT_FALSE(outputControls.empty());
    expectNothingOverlaps(osc, "before");
    const int serial = osc.serial();

    GroupPointer pointer(*osc.editor, "unison");
    pointer.moveBy(unisonBelowOutput(osc));
    EXPECT_TRUE(osc.editor->isDraggingGroupForTest());
    EXPECT_EQ(osc.serial(), serial) << "the drag only previews";
    pointer.release();

    const auto layout = osc.rig.storedLayout(osc.id);
    ASSERT_TRUE(layout.has_value());
    EXPECT_EQ(sectionIds(*layout).joinIntoString(","), "waveform,pitch,output,unison,footer");
    EXPECT_EQ(osc.serial(), serial + 1) << "one write for the whole move";
    EXPECT_EQ(osc.announced(), "Unison moved below Output");

    const auto cellsAfter = cellRects(osc.card());
    const auto blocksAfter = blocksOf(*osc.editor, osc.card().getCardBody()->getPlan());
    EXPECT_EQ(osc.card().getHeight(), heightBefore) << "the card keeps its height";
    EXPECT_EQ(blocksAfter.at("pitch"), blocksBefore.at("pitch")) << "groups above the run stay put";
    EXPECT_EQ(blocksAfter.at("output").getY(), blocksBefore.at("unison").getY()) << "Output takes Unison's place";
    EXPECT_EQ(blocksAfter.at("unison").getBottom(), blocksBefore.at("output").getBottom()) << "Unison is last";
    for (const auto& id : unisonControls)
        if (cellsBefore.count(id) != 0) {
            const int was = cellsBefore.at(id).getY() - blocksBefore.at("unison").getY();
            EXPECT_EQ(cellsAfter.at(id).getY() - blocksAfter.at("unison").getY(), was)
                << id << " keeps its place in Unison";
            EXPECT_EQ(cellsAfter.at(id).getX(), cellsBefore.at(id).getX()) << id;
        }
    for (const auto& id : outputControls) {
        const int was = cellsBefore.at(id).getY() - blocksBefore.at("output").getY();
        EXPECT_EQ(cellsAfter.at(id).getY() - blocksAfter.at("output").getY(), was)
            << id << " keeps its place in Output";
    }
    expectNothingOverlaps(osc, "after the move");

    ASSERT_TRUE(undoWithEditorOpen(osc, "unison"));
    EXPECT_FALSE(osc.rig.storedLayout(osc.id).has_value()) << "one Cmd+Z restores the layout the card opened with";
    EXPECT_EQ(cellRects(osc.card()), cellsBefore);
    EXPECT_EQ(osc.editor->getGroupBlockForTest("unison"), blocksBefore.at("unison"));
    expectNothingOverlaps(osc, "after undo");

    ASSERT_TRUE(redoWithEditorOpen(osc, "unison"));
    EXPECT_EQ(cellRects(osc.card()), cellsAfter) << "Cmd+Shift+Z brings the move back";
    EXPECT_EQ(osc.editor->getGroupBlockForTest("unison"), blocksAfter.at("unison"));
}

// Regression guard for the preview-vs-commit kind: at the last drag frame the groups that make room stand where the
// drop puts them, and the plan the drag holds for the dragged one is the place the write gives it.
TEST(OnCardGroupMoveEditor, WhatTheDragShowsIsWhatTheDropWrites) {
    Oscillator osc;
    ASSERT_NE(osc.editor, nullptr);
    GroupPointer pointer(*osc.editor, "unison");
    pointer.moveBy(unisonBelowOutput(osc));
    ASSERT_TRUE(osc.editor->isDraggingGroupForTest());

    const auto previewCells = cellRects(osc.card());
    const auto plan = osc.editor->getGroupDropPlanForTest();
    ASSERT_EQ(plan.size(), 3u);
    std::map<juce::String, int> planned(plan.begin(), plan.end());
    const auto unisonControls = controlsOf(osc.card().getCardBody()->getPlan(), "unison");
    const auto outputControls = controlsOf(osc.card().getCardBody()->getPlan(), "output");
    pointer.release();

    const auto& after = osc.card().getCardBody()->getPlan();
    const auto blocks = blocksOf(*osc.editor, after);
    for (const auto& [id, top] : planned)
        EXPECT_EQ(blocks.at(id).getY(), top) << id << " lands where the drag planned it";
    const auto committed = cellRects(osc.card());
    for (const auto& id : outputControls)
        EXPECT_EQ(previewCells.at(id), committed.at(id)) << id << " made room exactly where it ends up";
    for (const auto& id : unisonControls)
        if (previewCells.count(id) != 0 && committed.count(id) != 0)
            EXPECT_EQ(previewCells.at(id), committed.at(id)) << id << " held at the last slot, where the drop puts it";
}

TEST(OnCardGroupMoveEditor, DroppingAGroupBackWhereItBeganWritesNothing) {
    Oscillator osc;
    ASSERT_NE(osc.editor, nullptr);
    const auto cellsBefore = cellRects(osc.card());
    const int serial = osc.serial();
    GroupPointer pointer(*osc.editor, "unison");
    pointer.moveBy({0, 12});
    EXPECT_TRUE(osc.editor->isDraggingGroupForTest());
    pointer.moveBy({0, -12});
    pointer.release();
    EXPECT_EQ(osc.serial(), serial);
    EXPECT_FALSE(osc.rig.storedLayout(osc.id).has_value());
    EXPECT_EQ(cellRects(osc.card()), cellsBefore) << "everything is back in place";
}

TEST(OnCardGroupMoveEditor, EscapeMidDragPutsTheGroupBackAndWritesNothing) {
    Oscillator osc;
    ASSERT_NE(osc.editor, nullptr);
    const auto cellsBefore = cellRects(osc.card());
    const int serial = osc.serial();
    GroupPointer pointer(*osc.editor, "unison");
    pointer.moveBy(unisonBelowOutput(osc));
    ASSERT_TRUE(osc.editor->sendEscapeToGroupDragForTest());
    EXPECT_EQ(cellRects(osc.card()), cellsBefore) << "back where it was picked up";
    pointer.release();
    EXPECT_EQ(osc.serial(), serial);
    EXPECT_FALSE(osc.rig.storedLayout(osc.id).has_value());
}

TEST(OnCardGroupMoveEditor, AltDownOnAFocusedTitleMovesTheGroupOnePlaceAndAnnouncesIt) {
    Oscillator osc;
    ASSERT_NE(osc.editor, nullptr);
    const auto blocksBefore = blocksOf(*osc.editor, osc.card().getCardBody()->getPlan());
    const int serial = osc.serial();
    auto* handle = osc.editor->getGroupHandleForTest("unison");
    EXPECT_TRUE(handle->keyPressed(juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys(kAlt), 0)));

    const auto layout = osc.rig.storedLayout(osc.id);
    ASSERT_TRUE(layout.has_value());
    EXPECT_EQ(sectionIds(*layout).joinIntoString(","), "waveform,pitch,output,unison,footer");
    EXPECT_EQ(osc.serial(), serial + 1);
    EXPECT_EQ(osc.announced(), "Unison moved below Output");
    EXPECT_EQ(osc.editor->getGroupBlockForTest("unison").getBottom(), blocksBefore.at("output").getBottom());
    expectNothingOverlaps(osc, "after Alt+Down");
    ASSERT_NE(osc.editor->getGroupHandleForTest("unison"), nullptr);

    // The same group, still the one a key reaches: Alt+Up brings it back as another step.
    EXPECT_TRUE(osc.editor->getGroupHandleForTest("unison")->keyPressed(
        juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys(kAlt), 0)));
    EXPECT_EQ(osc.announced(), "Unison moved above Output");
    EXPECT_EQ(osc.serial(), serial + 2);
    EXPECT_EQ(osc.editor->getGroupBlockForTest("unison"), blocksBefore.at("unison"));

    ASSERT_TRUE(undoWithEditorOpen(osc, "unison"));
    EXPECT_EQ(sectionIds(*osc.rig.storedLayout(osc.id)).joinIntoString(","), "waveform,pitch,output,unison,footer")
        << "each key press is one undo step";
}

TEST(OnCardGroupMoveEditor, AltKeysAtTheEdgesMoveNothingAndSayWhy) {
    Oscillator osc;
    ASSERT_NE(osc.editor, nullptr);
    const int serial = osc.serial();
    EXPECT_TRUE(osc.editor->getGroupHandleForTest("pitch")->keyPressed(
        juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys(kAlt), 0)));
    EXPECT_EQ(osc.announced(), "Pitch is already first");
    EXPECT_TRUE(osc.editor->getGroupHandleForTest("output")->keyPressed(
        juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys(kAlt), 0)));
    EXPECT_EQ(osc.announced(), "Output is already last");
    EXPECT_EQ(osc.serial(), serial);
    EXPECT_FALSE(osc.editor->getGroupHandleForTest("pitch")->keyPressed(
        juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys(), 0)))
        << "a plain arrow is left to bubble";
}

// On the moved group: Add control puts a hidden Detune back in the Unison group wherever
// that group now stands.
TEST(OnCardGroupMoveEditor, ADetuneAddedBackAfterTheGroupMovedLandsInItsOwnGroup) {
    Oscillator osc;
    ASSERT_NE(osc.editor, nullptr);
    hideThroughPanel(osc.rig, *osc.editor, "detune");
    osc.editor->getGroupHandleForTest("unison")->keyPressed(
        juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys(kAlt), 0));
    ASSERT_EQ(sectionIds(*osc.rig.storedLayout(osc.id)).joinIntoString(","), "waveform,pitch,output,unison,footer");

    auto* panel = openAddPanel(osc.rig, *osc.editor);
    ASSERT_NE(panel, nullptr);
    clickRow(*panel->getRowForTest("detune"));

    const auto layout = osc.rig.storedLayout(osc.id);
    ASSERT_TRUE(layout.has_value());
    juce::String section;
    for (const auto& s : layout->sections)
        for (const auto& item : s.items)
            if (const auto* param = std::get_if<synth::CardParamItem>(&item);
                param != nullptr && param->paramId == "detune")
                section = s.id;
    EXPECT_EQ(section, "unison");
    const auto block = osc.editor->getGroupBlockForTest("unison");
    EXPECT_TRUE(block.contains(osc.editor->getCellRectForTest("detune"))) << "drawn inside the moved Unison group";
    expectNothingOverlaps(osc, "after the add");
}

// Guard for the layout-overlap kind across card types: every title that can move goes one place down with Alt+Down
// (the last one up), and afterwards no two controls or groups overlap and the card keeps its height.
TEST(OnCardGroupMoveEditor, MovingEachGroupOfEveryCardTypeNeverOverlapsAnything) {
    const auto sweep = [](std::unique_ptr<ModuleBase> module, const juce::String& name) {
        OnCardRig rig;
        const auto id = rig.add(std::move(module), 100, 100);
        auto* editor = rig.openOnCard(id);
        ASSERT_NE(editor, nullptr) << name;
        std::vector<juce::String> ids;
        for (const auto& section : rig.card(id)->getCardBody()->getPlan().sections)
            if (editor->getGroupHandleForTest(section.id) != nullptr)
                ids.push_back(section.id);
        const auto height = rig.card(id)->getHeight();
        for (const auto& group : ids) {
            auto* handle = editor->getGroupHandleForTest(group);
            ASSERT_NE(handle, nullptr) << name << " " << group;
            handle->keyPressed(juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys(kAlt), 0));
            handle = editor->getGroupHandleForTest(group);
            if (handle != nullptr)
                handle->keyPressed(juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys(kAlt), 0));
            const auto rects = cellRects(*rig.card(id));
            for (auto a = rects.begin(); a != rects.end(); ++a)
                for (auto b = std::next(a); b != rects.end(); ++b)
                    EXPECT_FALSE(a->second.intersects(b->second))
                        << name << " " << group << ": " << a->first << " over " << b->first;
            const auto blocks = blocksOf(*editor, rig.card(id)->getCardBody()->getPlan());
            for (auto a = blocks.begin(); a != blocks.end(); ++a)
                for (auto b = std::next(a); b != blocks.end(); ++b)
                    EXPECT_FALSE(a->second.intersects(b->second))
                        << name << ": group " << a->first << " over " << b->first;
            EXPECT_EQ(rig.card(id)->getHeight(), height) << name << " " << group;
        }
    };
    sweep(std::make_unique<OscillatorModule>(), "oscillator");
    sweep(std::make_unique<FilterModule>(), "filter");
    sweep(std::make_unique<LFOModule>(), "lfo");
}

namespace {

/** Cmd+Z or Cmd+Shift+Z as the app takes it: the focused title leaves the key, then the app undoes or redoes. */
bool undoOrRedo(Oscillator& osc, bool redo) {
    const int mods = juce::ModifierKeys::commandModifier | (redo ? juce::ModifierKeys::shiftModifier : 0);
    EXPECT_FALSE(
        osc.editor->getGroupHandleForTest("unison")->keyPressed(juce::KeyPress('z', juce::ModifierKeys(mods), 0)));
    return redo ? osc.rig.canvas.undo.redo() : osc.rig.canvas.undo.undo();
}

/** Where the Unison title and its handle are drawn now. */
struct TitleAt {
    int header = 0;
    int handle = 0;
};

TitleAt unisonTitle(Oscillator& osc) {
    for (const auto& section : osc.card().getCardBody()->getPlan().sections)
        if (section.id == "unison")
            return {section.header->getY(), osc.editor->getGroupHandleForTest("unison")->getY()};
    return {};
}

} // namespace

// Regression guard for the undo-gap kind: Cmd+Z and Cmd+Shift+Z of a group move glide the title and its outline
// with the group, the way the controls glide: no frame shows the restored place first, and the title travels one
// way only.
TEST(OnCardGroupMoveEditor, UndoAndRedoGlideTheTitleAndItsOutlineWithTheGroup) {
    Oscillator osc;
    synth::ui::setReducedMotionForTest(false);
    ASSERT_NE(osc.editor, nullptr);
    const int openedY = unisonTitle(osc).header;
    osc.editor->getGroupHandleForTest("unison")->keyPressed(
        juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys(kAlt), 0));
    osc.editor->finishMotionForTest();
    const int movedY = unisonTitle(osc).header;
    ASSERT_GT(movedY, openedY);
    osc.editor->setForceAnimateForTest(true);

    ASSERT_TRUE(undoOrRedo(osc, false));
    EXPECT_EQ(unisonTitle(osc).header, movedY) << "the first frame after Cmd+Z still shows the title where it was";
    EXPECT_TRUE(osc.editor->isGlidingForTest());
    int previous = movedY;
    bool moved = false;
    for (float ms = 0.0f; ms <= 160.0f; ms += 16.0f) {
        osc.editor->stepGlideForTest(ms);
        const auto at = unisonTitle(osc);
        EXPECT_LE(at.header, previous) << "one way only, at " << ms << " ms";
        EXPECT_GE(at.header, openedY);
        EXPECT_EQ(at.handle, at.header) << "the outline goes with the title";
        moved = moved || at.header < movedY;
        previous = at.header;
    }
    EXPECT_TRUE(moved);
    osc.editor->stepGlideForTest(1000.0f);
    EXPECT_EQ(unisonTitle(osc).header, openedY);
    EXPECT_EQ(unisonTitle(osc).handle, openedY);

    ASSERT_TRUE(undoOrRedo(osc, true));
    EXPECT_EQ(unisonTitle(osc).header, openedY) << "redo starts from the restored place";
    previous = openedY;
    for (float ms = 0.0f; ms <= 160.0f; ms += 16.0f) {
        osc.editor->stepGlideForTest(ms);
        EXPECT_GE(unisonTitle(osc).header, previous) << ms;
        EXPECT_LE(unisonTitle(osc).header, movedY);
        previous = unisonTitle(osc).header;
    }
    osc.editor->stepGlideForTest(1000.0f);
    EXPECT_EQ(unisonTitle(osc).header, movedY);
}

TEST(OnCardGroupMoveEditor, UnderReduceMotionUndoPutsTheTitleBackAtOnce) {
    Oscillator osc;
    synth::ui::setReducedMotionForTest(true);
    ASSERT_NE(osc.editor, nullptr);
    const int openedY = unisonTitle(osc).header;
    osc.editor->getGroupHandleForTest("unison")->keyPressed(
        juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys(kAlt), 0));
    osc.editor->setForceAnimateForTest(true);
    ASSERT_TRUE(undoOrRedo(osc, false));
    EXPECT_FALSE(osc.editor->isGlidingForTest());
    EXPECT_EQ(unisonTitle(osc).header, openedY);
    synth::ui::setReducedMotionForTest(false);
}
