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

#include "../../Layout/ViewportDragFollow.h"
#include "AppUndoManager.h"
#include "Modules/FilterModule.h"
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

    // `wide` makes the view bigger than the canvas frame (the frame starts at 1800 x 1100) and adds no far card, so
    // the pointer can be held past the frame's border.
    explicit Canvas(bool wide = false) {
        editor.setSize(wide ? 2400 : 800, wide ? 1500 : 600);
        editor.getEdgeScroll().setPointerForTest([this] { return pointer; });
        // Content well past the right edge and the bottom, so there is room to scroll into.
        if (!wide)
            addModuleAt(editor, engine, std::make_unique<NoiseModule>(), 1800, 1400);
    }
    // The canvas frame in the view's own coordinates (the view is not zoomed or panned at the start).
    juce::Rectangle<float> frameInView() const {
        const auto doc = editor.getViewDoc();
        const auto f = editor.getCanvasFrameRect();
        return {f.getX() * doc.zoom + doc.panX, f.getY() * doc.zoom + doc.panY, f.getWidth() * doc.zoom,
                f.getHeight() * doc.zoom};
    }
    size_t moduleCount() { return editor.getModuleComponents().size(); }
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
    /** True when something is drawn at the pointer (a card, a cable end, a macro); a box that grows has nothing. */
    virtual bool drawnAtPointer() const { return true; }
    /** True when what is drawn at the pointer is inside the view. */
    virtual bool drawnInside(Canvas&) { return true; }
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
    // The card still meets the view and the point it was grabbed by is inside it.
    bool drawnInside(Canvas& c) override {
        const auto bounds = c.editor.getLocalArea(card, card->getLocalBounds());
        return bounds.intersects(c.editor.getLocalBounds()) &&
               c.editor.getLocalBounds().contains(c.editor.getLocalPoint(card, pressInCard));
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
    // The drawn cable end is where the drag left it (the screen position GraphEditor keeps for it).
    bool drawnInside(Canvas& c) override {
        return c.editor.getLocalBounds().contains(
            c.editor.getLocalPoint(nullptr, c.editor.getEdgeScroll().cableEndForTest()));
    }
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
    bool drawnAtPointer() const override { return false; } // the box grows rather than moves
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
    bool drawnInside(Canvas& c) override {
        // The chip's bounds are in canvas space; the view has scrolled, so it is placed in the editor first.
        const auto& view = c.editor.getViewDoc();
        const auto chip = c.editor.getMacroController().macroChipBounds(macroId);
        return (float)chip.getX() * view.zoom + view.panX < (float)c.editor.getWidth();
    }
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

        // Pressed with the pointer already inside the right edge zone (a jack scrolled up to the edge): held still the
        // canvas stays; a few pixels toward the edge, it pans.
        Canvas still;
        const juce::Point<int> inZone(775, 300), towardEdge(781, 300);
        viewport_drag_follow::expectPressInZoneHoldsStill({[&] {
                                                               still.pointer = inZone.toFloat();
                                                               g->press(still);
                                                               g->dragTo(still, inZone);
                                                           },
                                                           [&] { still.tick(1); },
                                                           [&] {
                                                               still.pointer = towardEdge.toFloat();
                                                               g->dragTo(still, towardEdge);
                                                           },
                                                           [&] { return (double)still.panX(); },
                                                           [&] { g->release(still, towardEdge); }});

        // Regression test for FRO2083: a real pointer dragged past the window keeps the canvas scrolling, and what is
        // dragged stays in view (a selection box grows, so nothing is drawn at the pointer).
        std::unique_ptr<Canvas> past;
        juce::Point<int> pastAt;
        viewport_drag_follow::expectPastWindowFollows(
            {[&] {
                 past = std::make_unique<Canvas>();
                 g->press(*past);
             },
             [&](float px) {
                 pastAt = {past->editor.getWidth() + (int)px, 300};
                 past->pointer = pastAt.toFloat();
                 g->dragTo(*past, pastAt);
             },
             [&] { past->tick(1); }, [&] { return (double)past->panX(); },
             g->drawnAtPointer() ? std::function<bool()>([&] { return g->drawnInside(*past); }) : nullptr,
             [&] { g->release(*past, pastAt); }});
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
            c.tick(8); // past the ease-in, so the pace is each pointer's own
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
        c.tick(4); // the speed eases out over a few frames
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

// A cable carried past the view edge to a card that was out of sight lands on it, and the whole drag (the scroll
// included) is one undo step.
TEST(CanvasEdgeScrollEveryDrag, ACableCarriedToACardThatScrolledInConnectsAsOneUndoStep) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    undo.setGraphEditor(&editor);
    editor.setSize(800, 600);
    juce::Point<float> pointer = kEdge.toFloat();
    editor.getEdgeScroll().setPointerForTest([&pointer] { return pointer; });
    auto* from = findComponent(editor, addModuleAt(editor, engine, std::make_unique<NoiseModule>(), 100, 200));
    auto* target = findComponent(editor, addModuleAt(editor, engine, std::make_unique<FilterModule>(), 1000, 200));
    ASSERT_NE(from, nullptr);
    ASSERT_NE(target, nullptr);
    ASSERT_GT(editor.getLocalArea(target, target->getLocalBounds()).getX(), editor.getWidth())
        << "sanity: the target starts out of sight";

    const auto connectionsBefore = engine.getGraph().getConnections().size();
    const int serialBefore = undo.getEditSerial();
    const auto jack = from->getPortCenter(0, false);
    pointer = editor.getLocalPoint(from, jack).toFloat(); // the press lands inside the view
    from->mouseDown(eventAt(*from, jack, jack, false));
    pointer = kEdge.toFloat();
    from->mouseDrag(eventAt(*from, from->getLocalPoint(&editor, kEdge), jack, true));
    for (int i = 0; i < 30; ++i)
        editor.getEdgeScroll().tickForTest();
    const auto drop = editor.getLocalPoint(target, target->getPortCenter(0, true));
    ASSERT_TRUE(editor.getLocalBounds().reduced(48).contains(drop)) << "the target's input scrolled into view";

    pointer = drop.toFloat();
    from->mouseDrag(eventAt(*from, from->getLocalPoint(&editor, drop), jack, true));
    from->mouseUp(eventAt(*from, from->getLocalPoint(&editor, drop), jack, true));
    EXPECT_FALSE(editor.getEdgeScroll().isActive());
    ASSERT_EQ(engine.getGraph().getConnections().size(), connectionsBefore + 1) << "the cable landed on the target";
    EXPECT_EQ(undo.getEditSerial(), serialBefore + 1) << "one edit for the whole drag";
    ASSERT_TRUE(undo.undo());
    EXPECT_EQ(engine.getGraph().getConnections().size(), connectionsBefore) << "one Cmd+Z takes the cable away";
}

// Regression test for FRO2264: a cable dragged past the canvas frame showed no limit. The zone a gesture works in is
// the same past the canvas frame as inside it. With the view wider than the frame, a cable end and a selection box keep
// following the pointer (no clamp), the "Edge of canvas" marker shows on every side the pointer is beyond, and a
// release there changes what a release on empty canvas inside the frame does not: no cable, no card, no frame growth. A
// macro moved by its name chip grows the frame ahead of itself like a card, so it has nothing to mark.
namespace {
struct Outcome {
    size_t modules = 0, connections = 0;
    juce::Rectangle<int> frameTarget;
    bool scrolling = false, dragging = false;
    unsigned marker = 0;
    bool operator==(const Outcome& o) const {
        return modules == o.modules && connections == o.connections && frameTarget == o.frameTarget &&
               scrolling == o.scrolling && dragging == o.dragging && marker == o.marker;
    }
};
Outcome outcomeOf(Canvas& c, bool dragging) {
    c.editor.getEdgeScroll().limitMarkerForTest().settleForTest();
    return {c.moduleCount(),
            c.engine.getGraph().getConnections().size(),
            c.editor.getCanvasFrameForTest().target(),
            c.editor.getEdgeScroll().isActive(),
            dragging,
            c.editor.getEdgeScroll().limitMarkerSidesForTest()};
}
} // namespace

TEST(CanvasEdgeScrollEveryDrag, PastTheCanvasFrameACableAndABoxFollowThePointerAreMarkedAndReleaseLikeEmptyCanvas) {
    const unsigned kRight = synth::ui::CanvasEdgeLimitMarker::Right;
    const unsigned kBottom = synth::ui::CanvasEdgeLimitMarker::Bottom;
    CableDrag cableGesture;
    SelectionBoxDrag boxGesture;
    for (Gesture* g : std::vector<Gesture*>{&cableGesture, &boxGesture}) {
        SCOPED_TRACE(g->name());
        // Released on empty canvas inside the frame: the baseline every release past it must equal.
        Canvas inside(true);
        g->press(inside);
        const juce::Point<int> emptyInside(900, 700);
        inside.pointer = emptyInside.toFloat();
        g->dragTo(inside, emptyInside);
        inside.tick(1);
        g->release(inside, emptyInside);
        const auto baseline = outcomeOf(inside, inside.editor.isMarqueeActive());

        for (const bool pastRight : {true, false}) {
            SCOPED_TRACE(pastRight ? "past the right border" : "past the bottom border");
            Canvas c(true);
            g->press(c);
            const auto frame = c.frameInView();
            ASSERT_LT(frame.getRight() + 150, (float)c.editor.getWidth()) << "sanity: the view is wider than the frame";
            ASSERT_LT(frame.getBottom() + 100, (float)c.editor.getHeight()) << "sanity: ... and taller";
            const auto target = c.editor.getCanvasFrameForTest().target();
            const juce::Point<int> past = pastRight ? juce::Point<int>((int)frame.getRight() + 150, 700)
                                                    : juce::Point<int>(900, (int)frame.getBottom() + 100);
            ASSERT_TRUE(c.editor.getLocalBounds().reduced(60).contains(past)) << "sanity: not in a pan zone";
            c.pointer = past.toFloat();
            g->dragTo(c, past);
            c.tick(2);

            auto& scroll = c.editor.getEdgeScroll();
            EXPECT_EQ(scroll.limitMarkerSidesForTest() & (pastRight ? kRight : kBottom), pastRight ? kRight : kBottom)
                << "the marker shows on the side the pointer is beyond";
            c.editor.getEdgeScroll().limitMarkerForTest().settleForTest();
            EXPECT_GT(scroll.limitMarkerForTest().alphaForTest(), 0.9f);
            if (g == &cableGesture)
                EXPECT_EQ(c.editor.getLocalPoint(nullptr, scroll.cableEndForTest()), past)
                    << "the cable end follows the pointer past the frame, unclamped";
            else
                EXPECT_TRUE(c.editor.isMarqueeActive()) << "the box keeps growing with the pointer";
            EXPECT_EQ(c.editor.getCanvasFrameForTest().target(), target) << "dragging past it does not grow the frame";

            g->release(c, past);
            const auto out = outcomeOf(c, c.editor.isMarqueeActive());
            EXPECT_TRUE(out == baseline) << "a release past the frame is a release on empty canvas";
            EXPECT_EQ(out.marker, 0u);
            if (g == &cableGesture)
                EXPECT_FALSE(c.editor.isCableDragFrom(static_cast<ModuleComponent&>(*cableGesture.card)));
        }
    }
}

TEST(CanvasEdgeScrollEveryDrag, PastTheCanvasFrameAMacroMovedByItsChipGrowsTheFrameAndMarksNothing) {
    MacroNameChipDrag chip;
    Canvas c(true);
    chip.press(c);
    const auto frame = c.frameInView();
    const auto target = c.editor.getCanvasFrameForTest().target();
    const juce::Point<int> past((int)frame.getRight() + 150, 700);
    c.pointer = past.toFloat();
    chip.dragTo(c, past);
    c.tick(2);
    EXPECT_EQ(c.editor.getEdgeScroll().limitMarkerSidesForTest(), 0u) << "the frame steps out ahead, nothing to mark";
    EXPECT_GT(c.editor.getCanvasFrameForTest().target().getRight(), target.getRight()) << "the frame grew ahead of it";
    c.editor.getCanvasFrameForTest().finishAnimationForTest();
    EXPECT_GE(c.editor.getCanvasFrameRect().getRight() * c.editor.getViewDoc().zoom + c.editor.getViewDoc().panX,
              (float)past.x)
        << "the pointer is inside the grown frame";
    const auto grown = c.editor.getCanvasFrameForTest().target();
    chip.release(c, past);
    EXPECT_EQ(c.editor.getCanvasFrameForTest().target(), grown) << "the drop keeps the frame the drag grew";
    EXPECT_EQ(c.editor.getEdgeScroll().limitMarkerSidesForTest(), 0u);
}
