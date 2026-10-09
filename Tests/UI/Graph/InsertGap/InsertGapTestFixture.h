#pragma once

// Shared canvas for the insert-between tests (Tests/UI/Graph/InsertGap/InsertGap*Tests.cpp): a real GraphEditor with an
// AppUndoManager, rows of real cards, and the real gestures (the library's DragAndDropTarget calls, synthesised
// ModuleComponent mouse events, Esc through GraphEditor::keyPressed). Header-only; not compiled on its own.

#include "../../../Macros/MacroContainer/MacroContainerTestHelpers.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "Modules/VCAModule.h"
#include "UI/Graph/CardGlideAnimator/CardGlideAnimator.h"
#include "UI/Graph/InsertGap/InsertGap.h"
#include "UI/Layout/InsertGap/InsertGapPlan.h"
#include "UI/Layout/ReducedMotion.h"
#include <gtest/gtest.h>
#include <map>
#include <set>

namespace insert_gap_test {

using Rect = juce::Rectangle<int>;
using synth::ui::AnimationMode;

/** Pins the Animations preference (and the OS's Reduce Motion answer) for one test, then restores both. */
struct MotionMode {
    explicit MotionMode(AnimationMode mode = AnimationMode::full) {
        synth::ui::setAnimationMode(mode);
        synth::ui::setReducedMotionForTest(false);
    }
    ~MotionMode() {
        synth::ui::setAnimationMode(AnimationMode::followSystem);
        synth::ui::setReducedMotionForTest(std::nullopt);
    }
};

inline juce::MouseEvent mouseAt(juce::Component& comp, juce::Point<int> local, juce::Point<int> down, bool dragged) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), local.toFloat(),
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), down.toFloat(), juce::Time::getCurrentTime(), 1,
                            dragged);
}

struct Canvas {
    MotionMode motion;
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    juce::Component source; // the library row a drag comes from

    explicit Canvas(AnimationMode mode = AnimationMode::full)
        : motion(mode) {
        undo.setGraphEditor(&editor);
        editor.setSize(9000, 5000);
        editor.setMacroDragWithoutCmdEnabled(false);
    }
    ~Canvas() { editor.finishCardGlideForTest(); }

    // ---- Cards ----
    NodeID add(std::unique_ptr<juce::AudioProcessor> processor, int x, int y) {
        return addModuleAt(editor, engine, std::move(processor), x, y);
    }
    NodeID filter(int x, int y) { return add(std::make_unique<FilterModule>(), x, y); }
    ModuleComponent* card(NodeID id) { return findComponent(editor, id); }
    Rect rect(NodeID id) {
        auto* c = card(id);
        return c != nullptr ? c->getBounds() : Rect();
    }
    juce::Point<int> stored(NodeID id) {
        auto* node = engine.getGraph().getNodeForId(id);
        return {(int)node->properties["x"], (int)node->properties["y"]};
    }
    void place(NodeID id, int x, int y) {
        auto* node = engine.getGraph().getNodeForId(id);
        node->properties.set("x", x);
        node->properties.set("y", y);
        card(id)->setTopLeftPosition(x, y);
    }
    /** `n` Filter cards in one row, `spacing` apart, from (x0, y). */
    std::vector<NodeID> filterRow(int n, int x0 = 400, int y = 400, int spacing = 40) {
        std::vector<NodeID> ids;
        for (int i = 0; i < n; ++i)
            ids.push_back(filter(x0, y + 3000)); // parked out of the way until measured
        const int w = rect(ids.front()).getWidth();
        for (int i = 0; i < n; ++i)
            place(ids[(size_t)i], x0 + i * (w + spacing), y);
        editor.finishCardGlideForTest();
        return ids;
    }
    std::map<juce::uint32, Rect> allRects() {
        std::map<juce::uint32, Rect> out;
        for (auto* c : editor.getModuleComponents())
            if (c != nullptr && c->isVisible())
                out[c->getNodeId().uid] = c->getBounds();
        return out;
    }
    std::set<juce::uint32> nodeIds() {
        std::set<juce::uint32> out;
        for (auto* n : engine.getGraph().getNodes())
            out.insert(n->nodeID.uid);
        return out;
    }

    // ---- Macros ----
    MacroGroupController& macros() { return editor.getMacroController(); }
    juce::String openMacro(const std::vector<NodeID>& ids) {
        editor.setSelectedNodes(ids);
        const auto id = macros().groupSelectionIntoMacro(/*autoCreatePorts=*/false);
        macros().setMacroCollapsed(id, false);
        editor.clearSelection();
        editor.finishCardGlideForTest();
        return id;
    }
    Rect hull(const juce::String& macroId) { return macros().macroHullBounds(macroId); }
    bool isMember(const juce::String& macroId, NodeID id) {
        const auto* m = editor.getMacros().find(macroId);
        return m != nullptr && m->hasMember(uuidOf(engine, id));
    }

    // ---- The gap ----
    InsertGap& gap() { return macros().insertGap(); }
    CardGlideAnimator& glide() { return editor.getCardGlideForTest(); }
    static juce::Point<int> sizeOf(const juce::String& type) { return GraphEditor::estimateModuleSize(type); }
    /** The width of the gap a `type` card needs in a row whose spacing is `spacing`. */
    static int shiftFor(const juce::String& type, int spacing = 40) {
        return synth::insert_gap::snapUp(sizeOf(type).x + spacing);
    }

    // ---- A library drag: the calls the DragAndDropContainer makes, with the cursor at canvas point `p` ----
    juce::DragAndDropTarget::SourceDetails details(const juce::String& type, juce::Point<int> p) {
        return juce::DragAndDropTarget::SourceDetails(juce::var(type), &source, p);
    }
    void libraryEnter(const juce::String& type, juce::Point<int> p) {
        editor.itemDragEnter(details(type, p));
        editor.itemDragMove(details(type, p));
    }
    void libraryMove(const juce::String& type, juce::Point<int> p) { editor.itemDragMove(details(type, p)); }
    /** What JUCE does when the drag leaves the canvas, or when Esc cancels it. */
    void libraryExit(const juce::String& type, juce::Point<int> p) { editor.itemDragExit(details(type, p)); }
    /** Drops at `p` (after a move there) and returns the new node, or {} when none was made. */
    NodeID libraryDrop(const juce::String& type, juce::Point<int> p) {
        const auto before = nodeIds();
        editor.itemDragMove(details(type, p));
        editor.itemDropped(details(type, p));
        for (auto* n : engine.getGraph().getNodes())
            if (before.count(n->nodeID.uid) == 0 && !macros().nodeIsMacroPort(n->nodeID))
                return n->nodeID;
        return {};
    }
    /** The whole library gesture in one: enter at `p`, drop there. */
    NodeID dropBetween(const juce::String& type, juce::Point<int> p) {
        libraryEnter(type, p);
        return libraryDrop(type, p);
    }

    // ---- A canvas card drag through the card's own mouse handlers ----
    juce::Point<int> pressAt;
    void press(NodeID id) {
        auto* c = card(id);
        pressAt = {c->getWidth() / 2, ModuleComponent::kHeaderHeight + 10};
        c->mouseDown(mouseAt(*c, pressAt, pressAt, false));
    }
    /** Drags the pressed card so that its top-left lands on `topLeft`. */
    void dragTo(NodeID id, juce::Point<int> topLeft) {
        auto* c = card(id);
        c->mouseDrag(mouseAt(*c, pressAt + (topLeft - c->getPosition()), pressAt, true));
    }
    void release(NodeID id) {
        auto* c = card(id);
        c->mouseUp(mouseAt(*c, pressAt, pressAt, true));
    }
    /** Centres the dragged card on `centre`. */
    void dragCentreTo(NodeID id, juce::Point<int> centre) {
        const auto r = rect(id);
        dragTo(id, centre - juce::Point<int>(r.getWidth() / 2, r.getHeight() / 2));
    }
    bool pressEscape() { return editor.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)); }
};

/** The pointer in the middle of the gap between two cards of a row. */
inline juce::Point<int> between(const Rect& a, const Rect& b) {
    return {(a.getRight() + b.getX()) / 2, (a.getCentreY() + b.getCentreY()) / 2};
}

} // namespace insert_gap_test
