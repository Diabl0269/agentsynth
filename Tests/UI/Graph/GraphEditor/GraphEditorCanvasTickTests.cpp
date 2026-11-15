// GraphEditorCanvasTickTests.cpp
//
// What the 30 Hz canvas tick, a pan and a repeat paint cost, as work counts rather than timings
// (docs/layout/rendering.md#per-frame-work-does-not-grow-with-the-patch): the tick keeps the cable memo and refreshes
// the values cables are drawn with in place, refits the canvas frame only after something moved, a pan rebuilds no
// cable, and a paint measures no macro border that has not moved since the last one. Every change that moves a cable
// end still reaches the memo: a card moving, a control moving inside a card, a graph edit made behind the editor's
// back, a routing appearing.

#include "../../../Macros/MacroContainer/MacroContainerTestHelpers.h"
#include "../../../TestWait.h"

#include "AppUndoManager.h"
#include "Modules/FilterModule.h"
#include "Modules/LFOModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/CanvasLayer/CanvasStaticLayer.h"
#include "UI/Graph/GraphEditor/GraphEditorPaintMemo.h"
#include "UI/Layout/CableCurve.h"
#include "UI/Layout/ReducedMotion.h"
#include <gtest/gtest.h>

namespace {

struct Canvas {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    NodeID osc, filter;

    Canvas() {
        undo.setGraphEditor(&editor);
        editor.setSize(3200, 2400);
        osc = addModuleAt(editor, engine, std::make_unique<OscillatorModule>(), 200, 300);
        filter = addModuleAt(editor, engine, std::make_unique<FilterModule>(), 900, 300);
        engine.getGraph().addConnection({{osc, 0}, {filter, 0}});
        editor.updateComponents();
        settle();
    }
    void tick() { static_cast<juce::Timer&>(editor).timerCallback(); }
    void paint() { (void)editor.createComponentSnapshot(editor.getLocalBounds(), true, 1.0f); }
    // Ticks and paints until nothing is left over from setup, then reads the memo once.
    void settle() {
        editor.finishCardGlideForTest();
        editor.finishHullGlideForTest();
        for (int i = 0; i < 2; ++i) {
            tick();
            paint();
        }
        (void)editor.buildVisibleCables();
    }
    int rebuilds() { return editor.getCableRebuildCountForTest(); }
    juce::String groupIntoOpenMacro(std::initializer_list<NodeID> ids) {
        editor.setSelectedNodes(ids);
        const auto id = editor.getMacroController().groupSelectionIntoMacro();
        editor.getMacroController().setMacroCollapsed(id, false);
        settle();
        return id;
    }
};

const graph_editor_types::VisibleCable* cableOfKind(GraphEditor& editor, graph_editor_types::VisibleCable::Kind kind) {
    for (const auto& c : editor.buildVisibleCables())
        if (c.kind == kind)
            return &c;
    return nullptr;
}

} // namespace

TEST(GraphEditorCanvasTick, AnIdleTickAndItsPaintRebuildNoCable) {
    Canvas c;
    const int before = c.rebuilds();
    for (int i = 0; i < 5; ++i) {
        c.tick();
        c.paint();
    }
    (void)c.editor.buildVisibleCables();
    EXPECT_EQ(c.rebuilds(), before) << "an idle frame redraws the cables it already has";
    EXPECT_EQ(c.editor.getVisibleCableCount(), 1);
}

TEST(GraphEditorCanvasTick, TheCardsOwnTimersDropNoCable) {
    Canvas c;
    const int before = c.rebuilds();
    for (int i = 0; i < 3; ++i) {
        for (auto* card : c.editor.getModuleComponents())
            static_cast<juce::Timer&>(*card).timerCallback(); // meters, step highlight, mod rings
        c.tick();
        c.paint();
    }
    (void)c.editor.buildVisibleCables();
    EXPECT_EQ(c.rebuilds(), before);
}

TEST(GraphEditorCanvasTick, ATickRefreshesTheAttenuverterAmountInPlace) {
    Canvas c;
    const auto lfo = addModuleAt(c.editor, c.engine, std::make_unique<LFOModule>(), 200, 900);
    const auto atten = c.engine.addModRouting(lfo, 0, c.filter, 1);
    ASSERT_NE(atten.uid, 0u);
    c.editor.updateComponents();
    c.settle();
    ASSERT_NE(cableOfKind(c.editor, graph_editor_types::VisibleCable::Kind::AttenuverterChain), nullptr);

    auto* amount = findParameterByID(c.engine.getGraph().getNodeForId(atten)->getProcessor(), "amount");
    ASSERT_NE(amount, nullptr);
    amount->setValueNotifyingHost(0.25f);
    const int before = c.rebuilds();
    c.tick();
    const auto* chain = cableOfKind(c.editor, graph_editor_types::VisibleCable::Kind::AttenuverterChain);
    ASSERT_NE(chain, nullptr);
    EXPECT_NEAR(chain->attenAmount, -0.5f, 1.0e-4f) << "the knob on the cable follows the amount";
    EXPECT_EQ(c.rebuilds(), before) << "without rebuilding the cables";
}

TEST(GraphEditorCanvasTick, ANewRoutingIsDrawnOnTheNextTick) {
    Canvas c;
    const auto lfo = addModuleAt(c.editor, c.engine, std::make_unique<LFOModule>(), 200, 900);
    c.settle();
    const int cables = c.editor.getVisibleCableCount();
    ASSERT_NE(c.engine.addModRouting(lfo, 0, c.filter, 1).uid, 0u);
    c.tick();
    EXPECT_EQ(c.editor.getVisibleCableCount(), cables + 1);
}

TEST(GraphEditorCanvasTick, ACableAddedBehindTheEditorsBackIsDrawnOnceTheGraphSaysSo) {
    Canvas c;
    const auto other = addModuleAt(c.editor, c.engine, std::make_unique<FilterModule>(), 900, 900);
    c.settle();
    const int cables = c.editor.getVisibleCableCount();
    c.engine.getGraph().addConnection({{c.osc, 0}, {other, 0}}); // no repaintCanvas(), no updateComponents()
    synth::test::flushMessageQueue();                            // the graph's change broadcast is asynchronous
    c.tick();
    EXPECT_EQ(c.editor.getVisibleCableCount(), cables + 1);
}

TEST(GraphEditorCanvasTick, ACardThatMovesTakesItsCablesWithIt) {
    Canvas c;
    auto* card = findComponent(c.editor, c.filter);
    const auto end = c.editor.buildVisibleCables().front().p2;
    card->setTopLeftPosition(card->getPosition() + juce::Point<int>(120, 80));
    const auto moved = c.editor.buildVisibleCables().front().p2;
    EXPECT_NEAR(moved.x - end.x, 120.0f, 0.5f);
    EXPECT_NEAR(moved.y - end.y, 80.0f, 0.5f);
}

TEST(GraphEditorCanvasTick, AControlThatMovesInsideACardDropsTheCableMemo) {
    Canvas c;
    auto* card = findComponent(c.editor, c.filter);
    juce::Slider* knob = nullptr;
    for (auto* child : card->getChildren())
        if (auto* s = dynamic_cast<juce::Slider*>(child); s != nullptr && knob == nullptr)
            knob = s;
    ASSERT_NE(knob, nullptr);
    const int before = c.rebuilds();
    knob->setBounds(knob->getBounds().translated(0, 6)); // what the on-card layout editor does to a knob
    (void)c.editor.buildVisibleCables();
    EXPECT_EQ(c.rebuilds(), before + 1) << "a knob landing moves with its knob";
}

TEST(GraphEditorCanvasTick, TheTickRefitsTheCanvasFrameOnlyAfterSomethingMoved) {
    Canvas c;
    graph_editor_paint::workCounters() = {};
    c.tick();
    c.tick();
    EXPECT_EQ(graph_editor_paint::workCounters().canvasFrameFits, 0) << "nothing moved";

    auto* card = findComponent(c.editor, c.filter);
    card->setTopLeftPosition(card->getPosition() + juce::Point<int>(400, 0));
    c.tick();
    c.tick();
    EXPECT_EQ(graph_editor_paint::workCounters().canvasFrameFits, 1) << "one fit for the move";
}

TEST(GraphEditorCanvasTick, APanRebuildsNoCableAndAZoomStillDoes) {
    Canvas c;
    c.groupIntoOpenMacro({c.osc, c.filter}); // an open macro's port jacks slide with the zoom
    const int before = c.rebuilds();
    juce::MouseWheelDetails wheel{};
    wheel.deltaY = 0.2f;
    wheel.isSmooth = true;
    const auto centre = c.editor.getLocalBounds().getCentre().toFloat();
    c.editor.mouseWheelMove(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), centre, {}, 0.0f, 0.0f,
                                             0.0f, 0.0f, 0.0f, &c.editor, &c.editor, juce::Time::getCurrentTime(),
                                             centre, juce::Time::getCurrentTime(), 1, false),
                            wheel);
    (void)c.editor.buildVisibleCables();
    EXPECT_EQ(c.rebuilds(), before) << "a pan moves no cable end";

    c.editor.zoomAroundCentre(-1.0f);
    (void)c.editor.buildVisibleCables();
    EXPECT_EQ(c.rebuilds(), before + 1);
    c.editor.settleZoomNowForTest();
}

TEST(GraphEditorCanvasTick, ARepaintMeasuresNoMacroBorderUntilAMemberMoves) {
    Canvas c;
    const auto farOsc = addModuleAt(c.editor, c.engine, std::make_unique<OscillatorModule>(), 2000, 1500);
    const auto farFilter = addModuleAt(c.editor, c.engine, std::make_unique<FilterModule>(), 2600, 1500);
    ASSERT_FALSE(c.groupIntoOpenMacro({c.osc, c.filter}).isEmpty());
    ASSERT_FALSE(c.groupIntoOpenMacro({farOsc, farFilter}).isEmpty());

    graph_editor_paint::workCounters() = {};
    c.tick();
    c.paint();
    EXPECT_EQ(graph_editor_paint::workCounters().hullComputations, 0) << "nothing moved since the last paint";
    EXPECT_GT(graph_editor_paint::workCounters().hullsPainted, 0);

    auto* card = findComponent(c.editor, c.filter);
    card->setTopLeftPosition(card->getPosition() + juce::Point<int>(60, 0));
    graph_editor_paint::workCounters() = {};
    c.paint();
    EXPECT_GE(graph_editor_paint::workCounters().hullComputations, 1) << "the moved member's border is measured";
    EXPECT_LE(graph_editor_paint::workCounters().hullComputations, 2) << "once per border";
}

// A membership change moves no card, so the border a paint measured just before it is the only one the memo holds.
TEST(GraphEditorCanvasTick, ABorderMeasuredBeforeAMembershipChangeStillGlides) {
    Canvas c;
    const auto macroId = c.groupIntoOpenMacro({c.osc, c.filter});
    const auto newcomer = addModuleAt(c.editor, c.engine, std::make_unique<FilterModule>(), 1500, 300);
    c.settle(); // the paint measured the border without the newcomer
    const auto before = c.editor.snapshotPaintedHulls();

    ASSERT_TRUE(c.editor.getMacros().addMember(macroId, uuidOf(c.engine, newcomer)));
    c.editor.glideHullsFrom(before);
    EXPECT_TRUE(c.editor.isHullGlideLiveForTest()) << "the border grows to the newcomer instead of snapping";
    c.editor.finishHullGlideForTest();
    EXPECT_TRUE(c.editor.paintedMacroHullBounds(macroId).contains(findComponent(c.editor, newcomer)->getBounds()));
}

// Zoomed in on a few cards (dragging a knob) the tick redraws only where its dots move, not every card in view.
TEST(GraphEditorCanvasTick, TheTickRepaintsOnlyTheVisibleCables) {
    struct RestoreMode {
        ~RestoreMode() { synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem); }
    } restore;
    synth::ui::setAnimationMode(synth::ui::AnimationMode::full);
    Canvas c;
    const auto lone = addModuleAt(c.editor, c.engine, std::make_unique<FilterModule>(), 300, 1500); // no cable
    c.settle();
    c.tick();
    const auto area = c.editor.getLastTickRepaintAreaForTest();
    const auto& cable = c.editor.buildVisibleCables().front();
    EXPECT_TRUE(area.toFloat().contains(
        synth::ui::cablePaintBounds(cable.p1, cable.p2).getIntersection(c.editor.getVisibleCanvasRect())))
        << "the dots move along the cable";
    EXPECT_FALSE(area.intersects(findComponent(c.editor, lone)->getBounds())) << "a card with no cable is left alone";
    EXPECT_TRUE(c.editor.getVisibleCanvasRect().contains(area.toFloat()));
}

// The flow dots follow the Animations preference: a tick steps them by 0.02 at full speed, 0.006 under Reduced and not
// at all under Off.
TEST(GraphEditorCanvasTick, TheCableFlowDotsStepByTheLoopRate) {
    struct RestoreMode {
        ~RestoreMode() { synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem); }
    } restore;
    Canvas c;
    const auto stepOf = [&](synth::ui::AnimationMode mode) {
        synth::ui::setAnimationMode(mode);
        const float before = c.editor.getCableFlowPhaseForTest();
        c.tick();
        return std::fmod(c.editor.getCableFlowPhaseForTest() - before + 1.0f, 1.0f);
    };
    EXPECT_NEAR(stepOf(synth::ui::AnimationMode::full), 0.02f, 1e-4f);
    EXPECT_NEAR(stepOf(synth::ui::AnimationMode::reduced), 0.006f, 1e-4f);
    EXPECT_NEAR(std::fmod(stepOf(synth::ui::AnimationMode::off) + 0.5f, 1.0f), 0.5f, 1e-6f);
}

// With the flow dots held (Animations Off) an idle canvas repaints nothing, however big the project: the
// Load test project burned about a core repainting every cable 30 times a second. A cable whose look changes still
// repaints.
TEST(GraphEditorCanvasTick, AnIdleCanvasWithHeldFlowDotsRepaintsNothing) {
    struct RestoreMode {
        ~RestoreMode() { synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem); }
    } restore;
    synth::ui::setAnimationMode(synth::ui::AnimationMode::off);
    Canvas c;
    c.tick();
    EXPECT_TRUE(c.editor.getLastTickRepaintAreaForTest().isEmpty()) << "nothing changed, nothing is redrawn";

    const auto lfo = addModuleAt(c.editor, c.engine, std::make_unique<LFOModule>(), 200, 900);
    const auto atten = c.engine.addModRouting(lfo, 0, c.filter, 1);
    c.editor.updateComponents();
    c.settle();
    c.tick();
    EXPECT_TRUE(c.editor.getLastTickRepaintAreaForTest().isEmpty());
    auto* amount = findParameterByID(c.engine.getGraph().getNodeForId(atten)->getProcessor(), "amount");
    ASSERT_NE(amount, nullptr);
    amount->setValueNotifyingHost(0.25f);
    c.tick();
    EXPECT_FALSE(c.editor.getLastTickRepaintAreaForTest().isEmpty()) << "a cable that changed is redrawn";
}

TEST(GraphEditorCanvasTick, AnIdleCanvasWithMovingFlowDotsKeepsRepainting) {
    struct RestoreMode {
        ~RestoreMode() { synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem); }
    } restore;
    synth::ui::setAnimationMode(synth::ui::AnimationMode::full);
    Canvas c;
    c.tick();
    EXPECT_FALSE(c.editor.getLastTickRepaintAreaForTest().isEmpty()) << "the dots move along the cable";
}

namespace {

struct FullMotion {
    FullMotion() { synth::ui::setAnimationMode(synth::ui::AnimationMode::full); }
    ~FullMotion() { synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem); }
};

synth::ui::CanvasStaticLayer& layerOf(GraphEditor& editor) {
    return graph_editor_paint::CanvasMemo::of(editor)->staticLayer();
}

// Ticks and paints until the canvas has been quiet long enough for the static layer to take its picture.
void idleUntilPictured(Canvas& c) {
    for (int i = 0; i <= synth::ui::CanvasStaticLayer::kQuietPaints + 1; ++i) {
        c.tick();
        c.paint();
    }
}

// What a window shows, read back on every platform (a native image reads zeros on Windows).
juce::Image render(GraphEditor& editor) {
    juce::Image image(juce::Image::ARGB, editor.getWidth(), editor.getHeight(), true, juce::SoftwareImageType());
    juce::Graphics g(image);
    editor.paintEntireComponent(g, true);
    return image;
}

// Blending the picture back can round a level or two; anything more is a real difference.
constexpr int kRasterRounding = 2;

// The pixels that differ by more than that, and where they are (for the failure message).
struct PixelDiff {
    int count = 0;
    juce::Rectangle<int> where;
    juce::String sample;
};

PixelDiff differingPixels(const juce::Image& a, const juce::Image& b) {
    PixelDiff diff;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x) {
            const auto p = a.getPixelAt(x, y), q = b.getPixelAt(x, y);
            const int off = juce::jmax(std::abs(p.getRed() - q.getRed()), std::abs(p.getGreen() - q.getGreen()),
                                       std::abs(p.getBlue() - q.getBlue()), std::abs(p.getAlpha() - q.getAlpha()));
            if (off > kRasterRounding) {
                const juce::Rectangle<int> px(x, y, 1, 1);
                diff.where = diff.count++ == 0 ? px : diff.where.getUnion(px);
                if (diff.count <= 3)
                    diff.sample << "(" << x << "," << y << ") " << p.toDisplayString(true) << " vs "
                                << q.toDisplayString(true) << "; ";
            }
        }
    return diff;
}

// `pairs` oscillator -> filter tracks, every pair in an open macro with an LFO on its cutoff.
void addTracks(Canvas& c, int pairs) {
    for (int i = 0; i < pairs; ++i) {
        const int y = 900 + i * 260;
        const auto osc = addModuleAt(c.editor, c.engine, std::make_unique<OscillatorModule>(), 200, y);
        const auto filter = addModuleAt(c.editor, c.engine, std::make_unique<FilterModule>(), 900, y);
        const auto lfo = addModuleAt(c.editor, c.engine, std::make_unique<LFOModule>(), 1600, y);
        c.engine.getGraph().addConnection({{osc, 0}, {filter, 0}});
        c.engine.addModRouting(lfo, 0, filter, 1);
        c.editor.updateComponents();
        c.groupIntoOpenMacro({osc, filter, lfo});
    }
    c.editor.setSize(3200, 1200 + pairs * 260);
    c.settle();
}

} // namespace

// Animations On: an idle canvas still moves its flow dots every tick, but the wires, macro borders and frame under them
// are drawn from one picture, so an idle frame strokes no cable and dashes no border however big the project (the
// Load test project idled at about half a core with Animations On). Only the dots are drawn, one pass per cable.
TEST(GraphEditorCanvasTick, AnIdleCanvasWithMovingFlowDotsDrawsNoWireOrBorderAtAnySize) {
    const FullMotion motion;
    for (const int pairs : {4, 16}) {
        Canvas c;
        c.editor.setVisible(true);
        addTracks(c, pairs);
        idleUntilPictured(c);
        graph_editor_paint::workCounters() = {};
        c.tick();
        c.paint();
        EXPECT_TRUE(layerOf(c.editor).lastPaintUsedPicture()) << pairs << " tracks";
        EXPECT_EQ(graph_editor_paint::workCounters().cablesPainted, 0) << pairs << " tracks";
        EXPECT_EQ(graph_editor_paint::workCounters().hullsPainted, 0) << pairs << " tracks";
        EXPECT_FALSE(c.editor.getLastTickRepaintAreaForTest().isEmpty()) << "the dots still move";
        EXPECT_EQ(layerOf(c.editor).picturesTaken(), 1) << "one picture for the whole idle stretch";
    }
}

// The picture is the same pixels a direct paint gives: an idle frame drawn from it equals the same frame painted
// after a repaint dropped it.
TEST(GraphEditorCanvasTick, AFrameDrawnFromThePictureLooksLikeADirectPaint) {
    const FullMotion motion;
    Canvas c;
    c.editor.setVisible(true);
    addTracks(c, 2);
    juce::Image fromPicture;
    for (int i = 0; i <= synth::ui::CanvasStaticLayer::kQuietPaints + 1; ++i) { // idle, read back in software
        c.tick();
        fromPicture = render(c.editor);
    }
    ASSERT_TRUE(layerOf(c.editor).lastPaintUsedPicture());
    c.editor.repaint(); // drops the picture; the flow phase stays where it is
    const auto direct = render(c.editor);
    ASSERT_FALSE(layerOf(c.editor).lastPaintUsedPicture());
    const auto diff = differingPixels(fromPicture, direct);
    EXPECT_EQ(diff.count, 0) << "in " << diff.where.toString() << ": " << diff.sample;
}

// Anything that repaints the canvas or the editor drops the picture, so the next frame draws what changed: a cable's
// look (the attenuverter amount), the editor's own repaint (a selection), a pan, a zoom.
TEST(GraphEditorCanvasTick, AnyRepaintOtherThanTheDotsDropsThePicture) {
    const FullMotion motion;
    Canvas c;
    c.editor.setVisible(true);
    const auto lfo = addModuleAt(c.editor, c.engine, std::make_unique<LFOModule>(), 200, 900);
    const auto atten = c.engine.addModRouting(lfo, 0, c.filter, 1);
    c.editor.updateComponents();
    c.settle();
    auto& layer = layerOf(c.editor);

    idleUntilPictured(c);
    ASSERT_TRUE(layer.lastPaintUsedPicture());
    findParameterByID(c.engine.getGraph().getNodeForId(atten)->getProcessor(), "amount")->setValueNotifyingHost(0.2f);
    c.tick();
    graph_editor_paint::workCounters() = {};
    c.paint();
    EXPECT_FALSE(layer.lastPaintUsedPicture()) << "a cable whose look changed is drawn again";
    EXPECT_GT(graph_editor_paint::workCounters().cablesPainted, 0);

    idleUntilPictured(c);
    ASSERT_TRUE(layer.lastPaintUsedPicture());
    c.editor.repaint();
    c.paint();
    EXPECT_FALSE(layer.lastPaintUsedPicture()) << "the editor's own repaint";

    idleUntilPictured(c);
    ASSERT_TRUE(layer.lastPaintUsedPicture());
    juce::MouseWheelDetails wheel{};
    wheel.deltaY = 0.2f;
    wheel.isSmooth = true;
    const auto centre = c.editor.getLocalBounds().getCentre().toFloat();
    c.editor.mouseWheelMove(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), centre, {}, 0.0f, 0.0f,
                                             0.0f, 0.0f, 0.0f, &c.editor, &c.editor, juce::Time::getCurrentTime(),
                                             centre, juce::Time::getCurrentTime(), 1, false),
                            wheel);
    c.paint();
    EXPECT_FALSE(layer.lastPaintUsedPicture()) << "a pan";

    idleUntilPictured(c);
    ASSERT_TRUE(layer.lastPaintUsedPicture());
    c.editor.zoomAroundCentre(-1.0f);
    c.paint();
    EXPECT_FALSE(layer.lastPaintUsedPicture()) << "a zoom";
}

// A card's own repaint (a meter, a mod ring: 15 times a second with audio running) never changes what is drawn under
// the cards, so it keeps the picture.
TEST(GraphEditorCanvasTick, ACardsOwnRepaintKeepsThePicture) {
    const FullMotion motion;
    Canvas c;
    c.editor.setVisible(true);
    idleUntilPictured(c);
    auto& layer = layerOf(c.editor);
    ASSERT_TRUE(layer.lastPaintUsedPicture());
    auto* card = c.editor.getModuleComponents().getFirst();
    ASSERT_NE(card, nullptr);
    card->repaint();
    card->repaint(card->getLocalBounds().removeFromTop(10));
    c.tick();
    c.paint();
    EXPECT_TRUE(layer.lastPaintUsedPicture());
    EXPECT_EQ(layer.picturesTaken(), 1);
}

// With audio running a modulated cable changes its look on nearly every tick. Its first change takes it out of the
// picture; from then on it is drawn over the picture on every frame and its changes keep the picture, so a playing
// project still draws only its moving cables.
TEST(GraphEditorCanvasTick, ACableWhoseLookKeepsChangingIsDrawnOverThePicture) {
    const FullMotion motion;
    Canvas c;
    c.editor.setVisible(true);
    const auto lfo = addModuleAt(c.editor, c.engine, std::make_unique<LFOModule>(), 200, 900);
    const auto atten = c.engine.addModRouting(lfo, 0, c.filter, 1);
    c.editor.updateComponents();
    c.settle();
    auto* amount = findParameterByID(c.engine.getGraph().getNodeForId(atten)->getProcessor(), "amount");
    auto& layer = layerOf(c.editor);

    amount->setValueNotifyingHost(0.2f); // the first change: the cable leaves the picture
    idleUntilPictured(c);
    const int pictures = layer.picturesTaken();
    for (const float value : {0.3f, 0.4f, 0.6f}) {
        amount->setValueNotifyingHost(value);
        c.tick();
        graph_editor_paint::workCounters() = {};
        c.paint();
        EXPECT_TRUE(layer.lastPaintUsedPicture()) << value;
        EXPECT_EQ(graph_editor_paint::workCounters().cablesPainted, 1) << "only the changing cable is stroked";
    }
    EXPECT_EQ(layer.picturesTaken(), pictures);
    const auto* chain = cableOfKind(c.editor, graph_editor_types::VisibleCable::Kind::AttenuverterChain);
    ASSERT_NE(chain, nullptr);
    EXPECT_NEAR(chain->attenAmount, 0.2f, 1.0e-4f) << "the knob on the cable still follows the amount";
}
