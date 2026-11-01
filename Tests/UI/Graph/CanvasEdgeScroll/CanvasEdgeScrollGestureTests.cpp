// CanvasEdgeScrollGestureTests.cpp
//
// A sibling-gap guard: every drag on the patch canvas scrolls it when the pointer is held at the edge of the
// view. The list below holds each gesture GraphEditor and its cards own; each one presses and drags
// through the real mouse handlers, parks the pointer in the edge zone and ticks the scroller by hand. A drag gesture
// that is added without an entry here is the one that will not scroll, so a new one is one more struct.
//
// Swept and left out on purpose: the empty-canvas pan (it is itself the scroll), the mod-amount ring drag and the card
// knob / fader / curve drags (they edit a value, nothing is carried across the canvas), the minimap drag (it already
// moves the view), and the library / sound-browser drag (CanvasEdgeScrollTests.cpp covers it through itemDragEnter).
// (docs/layout/layout.md#edge-scroll-while-dragging)

#include "../../../Macros/MacroContainer/MacroContainerTestHelpers.h"

#include "Modules/NoiseModule.h"
#include "Project/ViewDoc.h"
#include "UI/Graph/CanvasEdgeScroll/CanvasEdgeScrollDriver.h"
#include "UI/Macros/MacroCardComponent/MacroCardComponent.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <memory>
#include <vector>

namespace {

constexpr int kLeft = juce::ModifierKeys::leftButtonModifier;
constexpr int kLeftShift = kLeft | juce::ModifierKeys::shiftModifier;

juce::MouseEvent eventAt(juce::Component& comp, juce::Point<int> pos, juce::Point<int> downPos, bool dragged,
                         int mods = kLeft) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos.toFloat(), juce::ModifierKeys(mods),
                            0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &comp, &comp, juce::Time::getCurrentTime(), downPos.toFloat(),
                            juce::Time::getCurrentTime(), 1, dragged);
}

struct Canvas {
    AudioEngine engine;
    GraphEditor editor{engine, nullptr};
    juce::Point<float> pointer; // editor-local: what the scroller sees as the mouse

    Canvas() {
        editor.setSize(800, 600);
        editor.getEdgeScroll().setPointerForTest([this] { return pointer; });
        // Content well past the right edge and the bottom, so there is room to scroll into.
        addModuleAt(editor, engine, std::make_unique<NoiseModule>(), 1800, 1400);
    }
    NodeID module(int x, int y) { return addModuleAt(editor, engine, std::make_unique<NoiseModule>(), x, y); }
    float panX() const { return editor.getViewDoc().panX; }
    void tick(int times) {
        for (int i = 0; i < times; ++i)
            editor.getEdgeScroll().tickForTest();
    }
    bool escape() { return editor.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)); }
};

// One drag gesture, driven through the real mouse handlers of whatever owns it.
struct Gesture {
    virtual ~Gesture() = default;
    virtual const char* name() const = 0;
    virtual void press(Canvas& c) = 0;
    virtual void dragTo(Canvas& c, juce::Point<int> editorPoint) = 0;
    virtual void release(Canvas& c, juce::Point<int> editorPoint) = 0;
    /** True when the gesture's own state followed the scroll (the card moved, the box grew over what scrolled in). */
    virtual bool followed(Canvas& c) = 0;
    /** True when Esc cancels the gesture (checked after it is pressed). */
    virtual bool escapeCancels() const { return false; }
};

// A gesture a card owns: pressed on the card, dragged and released as that card's mouse events.
struct CardGesture : Gesture {
    juce::Component::SafePointer<juce::Component> card;
    juce::Point<int> pressInCard;
    juce::Point<int> startInEditor;
    virtual juce::Component* make(Canvas& c) = 0;
    virtual juce::Point<int> pressPoint(juce::Component& card) = 0;
    void press(Canvas& c) override {
        card = make(c);
        pressInCard = pressPoint(*card);
        startInEditor = c.editor.getLocalPoint(card, pressInCard);
        card->mouseDown(eventAt(*card, pressInCard, pressInCard, false));
    }
    void dragTo(Canvas& c, juce::Point<int> p) override {
        card->mouseDrag(eventAt(*card, card->getLocalPoint(&c.editor, p), pressInCard, true));
    }
    void release(Canvas& c, juce::Point<int> p) override {
        card->mouseUp(eventAt(*card, card->getLocalPoint(&c.editor, p), pressInCard, true));
    }
    bool pressPointMovedRight(Canvas& c) { return c.editor.getLocalPoint(card, pressInCard).x > startInEditor.x + 300; }
};

struct ModuleBodyDrag : CardGesture {
    const char* name() const override { return "ModuleBody"; }
    juce::Component* make(Canvas& c) override { return findComponent(c.editor, c.module(300, 200)); }
    juce::Point<int> pressPoint(juce::Component& comp) override {
        return {comp.getWidth() / 2, ModuleComponent::kHeaderHeight + 10};
    }
    // The card went with the canvas: it is still under the stationary pointer, far right of where it was pressed.
    bool followed(Canvas& c) override { return card->getX() > 300 + 300; }
};

struct MacroCardBodyDrag : CardGesture {
    const char* name() const override { return "MacroCardBody"; }
    juce::Component* make(Canvas& c) override {
        const auto a = c.module(300, 100);
        const auto b = c.module(300, 300);
        c.editor.setSelectedNodes({a, b});
        const auto id = c.editor.getMacroController().groupSelectionIntoMacro();
        return c.editor.getMacroController().getMacroCardForTest(id);
    }
    juce::Point<int> pressPoint(juce::Component& comp) override { return {comp.getWidth() / 2, comp.getHeight() - 20}; }
    bool followed(Canvas&) override { return card->getX() > 300 + 300; }
    bool escapeCancels() const override { return true; }
};

// A cable pulled from an output jack. Every way to start one goes through GraphEditor::beginConnectionDrag.
struct CableDrag : CardGesture {
    const char* name() const override { return "CableFromAnOutputJack"; }
    juce::Component* make(Canvas& c) override { return findComponent(c.editor, c.module(100, 200)); }
    juce::Point<int> pressPoint(juce::Component& comp) override {
        return static_cast<ModuleComponent&>(comp).getPortCenter(0, false);
    }
    bool followed(Canvas& c) override { return c.editor.isCableDragFrom(static_cast<ModuleComponent&>(*card)); }
    bool escapeCancels() const override { return true; }
};

// The selection box: Shift-drag on empty canvas (GraphEditor::mouseDown / mouseDrag / mouseUp).
struct SelectionBoxDrag : Gesture {
    NodeID scrolledIn;
    const char* name() const override { return "SelectionBox"; }
    void press(Canvas& c) override {
        scrolledIn = c.module(1000, 150); // right of the view at press
        c.editor.mouseDown(eventAt(c.editor, {50, 250}, {50, 250}, false, kLeftShift));
    }
    void dragTo(Canvas& c, juce::Point<int> p) override {
        c.editor.mouseDrag(eventAt(c.editor, p, {50, 250}, true, kLeftShift));
    }
    void release(Canvas& c, juce::Point<int> p) override {
        c.editor.mouseUp(eventAt(c.editor, p, {50, 250}, true, kLeftShift));
    }
    bool followed(Canvas& c) override {
        const auto sel = c.editor.getSelectedNodes();
        return c.editor.isMarqueeActive() && std::find(sel.begin(), sel.end(), scrolledIn) != sel.end();
    }
};

// An expanded macro's name chip: the whole macro moves as one body.
struct MacroNameChipDrag : Gesture {
    juce::Point<int> pressAt;
    juce::String macroId;
    const char* name() const override { return "MacroNameChip"; }
    void press(Canvas& c) override {
        const auto a = c.module(100, 100);
        const auto b = c.module(100, 300);
        c.editor.setSelectedNodes({a, b});
        macroId = c.editor.getMacroController().groupSelectionIntoMacro();
        c.editor.getMacroController().setMacroCollapsed(macroId, false);
        pressAt = c.editor.getMacroController().macroChipBounds(macroId).getCentre();
        c.editor.mouseDown(eventAt(c.editor, pressAt, pressAt, false));
    }
    void dragTo(Canvas& c, juce::Point<int> p) override { c.editor.mouseDrag(eventAt(c.editor, p, pressAt, true)); }
    void release(Canvas& c, juce::Point<int> p) override { c.editor.mouseUp(eventAt(c.editor, p, pressAt, true)); }
    bool followed(Canvas& c) override {
        // The hull travelled with the pointer, far right of its press.
        return c.editor.isMacroChipDragActive() &&
               c.editor.getMacroController().macroChipBounds(macroId).getX() > pressAt.x + 300;
    }
    bool escapeCancels() const override { return true; }
};

std::vector<std::unique_ptr<Gesture>> allGestures() {
    std::vector<std::unique_ptr<Gesture>> g;
    g.push_back(std::make_unique<ModuleBodyDrag>());
    g.push_back(std::make_unique<MacroCardBodyDrag>());
    g.push_back(std::make_unique<SelectionBoxDrag>());
    g.push_back(std::make_unique<CableDrag>());
    g.push_back(std::make_unique<MacroNameChipDrag>());
    return g;
}

const juce::Point<int> kEdge(797, 300);

} // namespace

// Regression test for FRO1151: a selection box, a cable and a macro name chip dragged to the window edge did not
// scroll.
TEST(CanvasEdgeScrollEveryDrag, TheCanvasScrollsAndTheGestureFollowsWhileThePointerIsHeldAtTheEdge) {
    for (auto& g : allGestures()) {
        SCOPED_TRACE(g->name());
        Canvas c;
        g->press(c);
        c.pointer = kEdge.toFloat();
        g->dragTo(c, kEdge);
        const float before = c.panX();
        c.tick(25);
        EXPECT_LT(c.panX(), before - 100.0f) << "the canvas scrolled to the right";
        EXPECT_TRUE(g->followed(c)) << "the gesture kept tracking the pointer";
        g->release(c, kEdge);
    }
}

TEST(CanvasEdgeScrollEveryDrag, TheCloserToTheEdgeTheFasterItScrolls) {
    for (auto& g : allGestures()) {
        SCOPED_TRACE(g->name());
        float step[2] = {};
        for (int i = 0; i < 2; ++i) {
            Canvas c;
            g->press(c);
            const juce::Point<int> at(i == 0 ? 770 : 797, 300);
            c.pointer = at.toFloat();
            g->dragTo(c, at);
            const float before = c.panX();
            c.tick(1);
            step[i] = before - c.panX();
            g->release(c, at);
        }
        EXPECT_GT(step[0], 0.0f);
        EXPECT_GT(step[1], step[0]);
    }
}

TEST(CanvasEdgeScrollEveryDrag, MovingTheMouseBackInwardStopsTheScroll) {
    for (auto& g : allGestures()) {
        SCOPED_TRACE(g->name());
        Canvas c;
        g->press(c);
        c.pointer = kEdge.toFloat();
        g->dragTo(c, kEdge);
        c.tick(3);
        c.pointer = {400.0f, 300.0f};
        g->dragTo(c, {400, 300});
        const float stopped = c.panX();
        c.tick(5);
        EXPECT_FLOAT_EQ(c.panX(), stopped);
        g->release(c, {400, 300});
    }
}

TEST(CanvasEdgeScrollEveryDrag, TheReleaseStopsTheScrollForGood) {
    for (auto& g : allGestures()) {
        SCOPED_TRACE(g->name());
        Canvas c;
        g->press(c);
        c.pointer = kEdge.toFloat();
        g->dragTo(c, kEdge);
        c.tick(3);
        g->release(c, kEdge);
        EXPECT_FALSE(c.editor.getEdgeScroll().isActive());
        const float stopped = c.panX();
        c.tick(5);
        EXPECT_FLOAT_EQ(c.panX(), stopped);
    }
}

TEST(CanvasEdgeScrollEveryDrag, EscapeStopsTheScrollWithTheGestureItCancels) {
    for (auto& g : allGestures()) {
        if (!g->escapeCancels())
            continue;
        SCOPED_TRACE(g->name());
        Canvas c;
        g->press(c);
        c.pointer = kEdge.toFloat();
        g->dragTo(c, kEdge);
        c.tick(2);
        ASSERT_TRUE(c.editor.getEdgeScroll().isActive()) << "sanity: the drag was scrolling";
        EXPECT_TRUE(c.escape());
        EXPECT_FALSE(c.editor.getEdgeScroll().isActive());
        const float stopped = c.panX();
        c.tick(5);
        EXPECT_FLOAT_EQ(c.panX(), stopped);
        g->release(c, kEdge);
    }
}
