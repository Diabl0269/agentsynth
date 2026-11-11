// A dragged canvas card lands where the drag showed it: a real mouse-down on a module card (and on a collapsed macro
// card), several drag events ending on top of another card with the pointer off the grid (so snapping and the push
// aside both apply), the spot the canvas shows after the LAST drag event (the landing ghost while one is drawn, else
// the card itself), then the release and the position the document holds. Style of ReorderDragPreviewCommit.h.

#include "../../../Macros/MacroContainer/MacroContainerTestHelpers.h"
#include "../../Layout/ReorderDragPreviewCommit.h"
#include "AppUndoManager.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"
#include <gtest/gtest.h>

namespace {

using namespace reorder_preview_commit;
using Pt = juce::Point<float>;

juce::MouseEvent cardEvent(juce::Component& card, Pt canvasPoint, Pt pressLocal, bool dragged) {
    const auto local = canvasPoint - card.getPosition().toFloat();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), local,
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &card, &card, juce::Time::getCurrentTime(), pressLocal, juce::Time::getCurrentTime(), 1,
                            dragged);
}

// The pointer path: from the card's centre to the target, the last point off the 8 px grid.
struct Gesture {
    Pt press;
    std::vector<Pt> path;
};

Gesture gestureOnto(const juce::Rectangle<int>& from, const juce::Rectangle<int>& target) {
    Gesture g;
    g.press = from.getCentre().toFloat();
    const Pt end = target.getCentre().toFloat() + Pt(13.0f, 7.0f);
    g.path = straightPath(g.press, end, 7);
    return g;
}

// Presses the card's centre, drags along the path, reads where the canvas shows the card, releases, reads the model.
// `committed` returns the position the document holds for the card after the release.
Outcome<int> moveCard(GraphEditor& editor, juce::Component& card, const Gesture& g,
                      const std::function<juce::Point<int>()>& committed) {
    const Pt pressLocal = g.press - card.getPosition().toFloat();
    Probe<int> probe;
    probe.send = [&](Phase phase, Pt p) {
        const auto event = cardEvent(card, p, pressLocal, phase != Phase::Down);
        if (phase == Phase::Down)
            card.mouseDown(event);
        else if (phase == Phase::Drag)
            card.mouseDrag(event);
        else
            card.mouseUp(event);
    };
    probe.shown = [&] {
        // A lone module drag draws one landing ghost; a folded card (which carries its hidden members) draws it as a
        // group ghost. Without either, the card itself is what the canvas shows.
        auto& dragDrop = editor.getDragDropController();
        auto at = card.getPosition();
        if (!dragDrop.getDragPreviewGhost().isEmpty())
            at = dragDrop.getDragPreviewGhost().getTopLeft();
        else if (!dragDrop.getGroupGhosts().empty())
            at = dragDrop.getGroupGhosts().front().getTopLeft();
        return std::vector<int>{at.x, at.y};
    };
    probe.committed = [&] {
        const auto at = committed();
        return std::vector<int>{at.x, at.y};
    };
    return dragAndRelease(probe, g.press, g.path);
}

} // namespace

TEST(CardMoveDragPreviewCommit, AModuleCardLandsWhereTheDragShowedIt) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(3000, 2000);
    const auto moving = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    const auto other = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 900, 100);
    auto* card = findComponent(editor, moving);
    auto* blocker = findComponent(editor, other);
    ASSERT_NE(card, nullptr);
    ASSERT_NE(blocker, nullptr);

    const auto out = moveCard(editor, *card, gestureOnto(card->getBounds(), blocker->getBounds()), [&] {
        auto* node = engine.getGraph().getNodeForId(moving);
        return juce::Point<int>((int)node->properties.getWithDefault("x", -1),
                                (int)node->properties.getWithDefault("y", -1));
    });
    ASSERT_EQ(out.preview.size(), 2u);
    EXPECT_EQ(out.preview, out.committed) << "the card landed somewhere other than the spot the drag showed";
    EXPECT_NE(out.committed, (std::vector<int>{100, 100})) << "the drag never moved the card";
}

TEST(CardMoveDragPreviewCommit, AMacroCardLandsWhereTheDragShowedIt) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor(engine, &undo);
    undo.setGraphEditor(&editor);
    editor.setSize(3000, 2000);
    const auto a = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 100, 100);
    const auto b = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 100, 600);
    const auto other = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 1400, 100);
    editor.setSelectedNodes({a, b});
    const auto macroId = editor.getMacroController().groupSelectionIntoMacro(false);
    editor.getMacroController().setMacroCollapsed(macroId, true);
    editor.finishCardGlideForTest();
    editor.setSelectedNodes({});
    auto* card = editor.getMacroController().getMacroCardForTest(macroId);
    auto* blocker = findComponent(editor, other);
    ASSERT_NE(card, nullptr);
    ASSERT_NE(blocker, nullptr);

    const auto out = moveCard(editor, *card, gestureOnto(card->getBounds(), blocker->getBounds()), [&] {
        const auto* macro = editor.getMacros().find(macroId);
        return macro != nullptr ? macro->bounds.getPosition() : juce::Point<int>(-1, -1);
    });
    ASSERT_EQ(out.preview.size(), 2u);
    EXPECT_EQ(out.preview, out.committed) << "the macro card landed somewhere other than the spot the drag showed";
}
