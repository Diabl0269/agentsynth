// AutomationLanesUndoGlideTests.cpp -- Cmd+Z and Cmd+Shift+Z of a reorder glide like the move did (docs/layout/
// animation.md "Undo and redo glide"): a modulator row moved with Alt+Up/Down and an automation lane moved within
// its track both slide from where they stood to their restored slots instead of snapping. The real AppUndoManager
// drives it. Headless: the animated path is forced (FadeAnimateGuard); the row glide is stepped by hand and the lane
// glide runs on a pinned clock that the test advances, never on frames.
//
// The selection side of a restore: Cmd+Z and Cmd+Shift+Z of a lane dot move keep the dot selected and in view. One
// table of gestures (a dot carried past the edge by a held Alt+Right, by a mouse drag parked at the edge, and moved
// while the view was then scrolled away), each under Full motion, Reduce Motion and Animations off: after the undo the
// dot is selected at its restored place with the keyboard cursor on it and the view shows it; the same after the redo.
// Under Full motion the view follows the gliding dot rather than jumping. A new editor with a selection and a
// scrolling view (the timeline's clips, the piano roll's notes) joins the table.

#include "../../Layout/FadeVisibilityTestGuard.h"
#include "../../Layout/KeyGlide/KeyGlideTestHost.h"
#include "AutomationLanesMenuFixture.h"
#include "AutomationLanesModulatorHost.h"
#include "AutomationLanesTestFixture.h"
#include "UI/Layout/KeyGlide/KeyedTween.h"
#include "UI/Layout/UIAnimation.h"
#include "UI/Timeline/AutomationLanes/PointSelection/LaneKeyStep.h"
#include <cmath>
#include <functional>
#include <memory>

using namespace modulator_rows_test;
using synth::TrackKind;

namespace {

juce::KeyPress altArrow(int keyCode) { return juce::KeyPress(keyCode, juce::ModifierKeys::altModifier, 0); }

void expectBetween(int value, int a, int b, const char* what) {
    EXPECT_GT(value, std::min(a, b)) << what;
    EXPECT_LT(value, std::max(a, b)) << what;
}

// Track "Bass" with an open cutoff lane routed from LFO 1..3.
struct RowScene {
    HostedLanes h;
    synth::TrackId bass;
    synth::LaneId cutoff;

    RowScene() {
        for (int i = 1; i <= 3; ++i)
            h.host.modulators.push_back(lfoInfo(juce::String(i)));
        bass = h.f.doc.addTrack(TrackKind::Midi, "Bass");
        cutoff = h.f.addLane(bass, "cutoff");
        h.f.panel.setTrackAutomationExpanded(bass, true);
    }
    synth::ui::TimelineAutomationLanes& lanes() { return h.f.panel.getAutomationLanes(); }
    synth::ui::ModulatorRow* row(int i) { return lanes().modulatorRowFor(cutoff, i); }
};

// Track "Bass" with three open lanes (cutoff, res, detune) and a pinned glide clock.
struct LaneScene : automation_lanes_test::LanesPanel {
    lane_menu_test::PickHost host;
    synth::TrackId bass;
    synth::LaneId cutoff, res, detune;
    double nowMs = 1000.0;

    LaneScene() {
        bass = doc.addTrack(TrackKind::Midi, "Bass");
        cutoff = addLane(bass, "cutoff");
        res = addLane(bass, "res");
        detune = addLane(bass, "detune");
        panel.setTrackHeaderHost(&host);
        panel.setTrackAutomationExpanded(bass, true);
        lanes().setLaneGlideClockForTest([this] { return nowMs; });
    }
    ~LaneScene() {
        lanes().setLaneGlideClockForTest({});
        panel.setTrackHeaderHost(nullptr);
    }
    synth::ui::TimelineAutomationLanes& lanes() { return panel.getAutomationLanes(); }
    int y(synth::LaneId id) { return panel.laneHeaderForTest(id)->getY(); }
    void advance(double ms) {
        nowMs += ms;
        lanes().stepLaneGlideForTest();
    }
};

} // namespace

// Regression test for FRO883: an undo of a modulator row move went through permuteModulators and snapped the rows back,
// since only the keyboard move itself started a glide.
TEST(AutomationLanesUndoGlideTest, UndoAndRedoOfAModulatorRowMoveGlideTheRowsBackAndForth) {
    RowScene s;
    FadeAnimateGuard animate;
    auto* moved = s.row(0);
    auto* neighbour = s.row(1);
    const int top = moved->getY();
    const int second = neighbour->getY();
    ASSERT_TRUE(moved->keyPressed(altArrow(juce::KeyPress::downKey)));
    s.lanes().stepModulatorGlideForTest(1.0f);
    ASSERT_EQ(s.row(1), moved);
    ASSERT_EQ(moved->getY(), second);

    ASSERT_TRUE(s.h.f.undo.undo());
    EXPECT_EQ(s.row(0), moved) << "the restored order";
    EXPECT_TRUE(s.lanes().isModulatorGlideAnimating());
    EXPECT_EQ(moved->getY(), second) << "starts where it stood";
    EXPECT_EQ(neighbour->getY(), top);
    s.lanes().stepModulatorGlideForTest(0.5f);
    expectBetween(moved->getY(), top, second, "the moved row glides up");
    expectBetween(neighbour->getY(), top, second, "its neighbour glides down");
    s.lanes().stepModulatorGlideForTest(1.0f);
    EXPECT_FALSE(s.lanes().isModulatorGlideAnimating());
    EXPECT_EQ(moved->getY(), top);
    EXPECT_EQ(neighbour->getY(), second);

    ASSERT_TRUE(s.h.f.undo.redo());
    EXPECT_EQ(s.row(1), moved);
    EXPECT_TRUE(s.lanes().isModulatorGlideAnimating()) << "redo glides too";
    s.lanes().stepModulatorGlideForTest(0.5f);
    expectBetween(moved->getY(), top, second, "the moved row glides down again");
    s.lanes().stepModulatorGlideForTest(1.0f);
    EXPECT_EQ(moved->getY(), second);
}

TEST(AutomationLanesUndoGlideTest, UnderReduceMotionAnUndoneModulatorRowMoveLandsAtOnce) {
    RowScene s;
    FadeAnimateGuard reduced(synth::ui::AnimationMode::reduced);
    auto* moved = s.row(0);
    const int top = moved->getY();
    ASSERT_TRUE(moved->keyPressed(altArrow(juce::KeyPress::downKey)));
    ASSERT_TRUE(s.h.f.undo.undo());
    EXPECT_FALSE(s.lanes().isModulatorGlideAnimating());
    EXPECT_EQ(moved->getY(), top);
}

// Regression test for FRO883: an undo of a lane move snapped the lanes back.
TEST(AutomationLanesUndoGlideTest, UndoAndRedoOfALaneMoveGlideTheLanesBackAndForth) {
    LaneScene f;
    FadeAnimateGuard animate;
    auto& header = *f.panel.laneHeaderForTest(f.cutoff);
    const int top = f.y(f.cutoff);
    const int second = f.y(f.res);
    ASSERT_TRUE(header.keyPressed(altArrow(juce::KeyPress::downKey)));
    f.advance(1000.0);
    ASSERT_EQ(f.y(f.cutoff), second);
    ASSERT_EQ(f.y(f.res), top);

    ASSERT_TRUE(f.undo.undo());
    EXPECT_EQ(f.doc.getTrack(f.bass)->lanes[0].id, f.cutoff) << "the restored order";
    EXPECT_TRUE(f.lanes().isLaneReorderActive()) << "a glide is running";
    EXPECT_EQ(f.y(f.cutoff), second) << "starts where it stood";
    EXPECT_EQ(f.y(f.res), top);
    f.advance(70.0);
    expectBetween(f.y(f.cutoff), top, second, "the lane glides up");
    expectBetween(f.y(f.res), top, second, "its neighbour glides down");
    f.advance(1000.0);
    EXPECT_FALSE(f.lanes().isLaneReorderActive());
    EXPECT_EQ(f.y(f.cutoff), top);
    EXPECT_EQ(f.y(f.res), second);

    ASSERT_TRUE(f.undo.redo());
    EXPECT_TRUE(f.lanes().isLaneReorderActive()) << "redo glides too";
    EXPECT_EQ(f.y(f.cutoff), top);
    f.advance(70.0);
    expectBetween(f.y(f.cutoff), top, second, "the lane glides down again");
    f.advance(1000.0);
    EXPECT_EQ(f.y(f.cutoff), second);
    EXPECT_EQ(f.y(f.res), top);
}

TEST(AutomationLanesUndoGlideTest, UnderReduceMotionAnUndoneLaneMoveLandsAtOnce) {
    LaneScene f;
    FadeAnimateGuard reduced(synth::ui::AnimationMode::reduced);
    const int top = f.y(f.cutoff);
    ASSERT_TRUE(f.panel.laneHeaderForTest(f.cutoff)->keyPressed(altArrow(juce::KeyPress::downKey)));
    ASSERT_TRUE(f.undo.undo());
    EXPECT_FALSE(f.lanes().isLaneReorderActive());
    EXPECT_EQ(f.y(f.cutoff), top);
}

// Regression test for FRO1197: the lane's curve is drawn through the tweened points, so a glide frame that repainted
// only the box around the moving dot left the line segments to its far neighbours (and the old dot) stale.
TEST(AutomationLanesUndoGlideTest, UndoOfAMovedPointRepaintsTheSegmentsToItsFarNeighbours) {
    automation_lanes_test::LanesPanel p;
    FadeAnimateGuard animate;
    const auto bass = p.doc.addTrack(TrackKind::Midi, "Bass");
    const auto lane = p.addLane(bass, "cutoff");
    p.panel.setTrackAutomationExpanded(bass, true);
    auto* editor = p.panel.laneEditorForTest(lane);
    editor->setTool(synth::ui::AutomationLaneEditor::Tool::Pointer);
    for (const auto& [beat, value] : {std::pair{0.5, 20.0}, {2.0, 60.0}, {3.5, 40.0}})
        p.doc.addBreakpoint(lane, beat, value);
    editor->getPointTweenForTest().forceAnimateForTest(true);

    const auto& view = p.panel.getViewState();
    const juce::Point<float> from{(float)view.beatToX(2.0), (float)editor->valueToY(60.0)};
    automation_lanes_test::dragAcross(*editor, from, {from.x, (float)editor->valueToY(10.0)}, 6);
    editor->getPointTweenForTest().stepForTest(1000.0);

    juce::RectangleList<int> dirty;
    synth::ui::setKeyedTweenDirtyProbeForTest([&](juce::Component& owner, juce::Rectangle<int> area) {
        if (&owner == editor)
            dirty.add(area);
    });
    ASSERT_TRUE(p.undo.undo());
    editor->getPointTweenForTest().stepForTest(32.0);
    synth::ui::setKeyedTweenDirtyProbeForTest({});

    for (const double beat : {0.5, 3.5}) {
        const auto neighbour =
            juce::Point<int>((int)view.beatToX(beat), (int)editor->valueToY(beat < 1.0 ? 20.0 : 40.0));
        EXPECT_TRUE(dirty.containsPoint(neighbour)) << "the far neighbour at beat " << beat << " is repainted";
    }
}

//==============================================================================
// Undo and redo keep the moved item selected and in view

namespace undo_keeps_selection {
using namespace automation_lanes_test;
using synth::ui::AutomationLaneEditor;

namespace {

enum class Motion { full, reduced, off };

const char* nameOf(Motion m) {
    switch (m) {
    case Motion::full:
        return "Full motion";
    case Motion::reduced:
        return "Reduce Motion";
    case Motion::off:
        return "Animations off";
    }
    return "";
}

struct MotionScope {
    explicit MotionScope(Motion m) {
        synth::ui::setAnimationMode(m == Motion::full      ? synth::ui::AnimationMode::full
                                    : m == Motion::reduced ? synth::ui::AnimationMode::reduced
                                                           : synth::ui::AnimationMode::off);
        synth::ui::setReducedMotionForTest(m == Motion::reduced);
    }
    ~MotionScope() {
        synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem);
        synth::ui::setReducedMotionForTest(std::nullopt);
    }
};

// One editor with a selection in a scrolling view. move() selects one item and moves it out of the view the test
// started with (one undo step) and leaves the move settled.
class SelectionSurface {
public:
    virtual ~SelectionSurface() = default;
    virtual void move() = 0;
    virtual bool undo() = 0;
    virtual bool redo() = 0;
    virtual double itemBeat() const = 0;   // where the moved item rests in the doc
    virtual bool itemSelected() const = 0; // selected, with the keyboard cursor on it
    virtual int selectedCount() const = 0;
    virtual double firstVisible() const = 0;
    virtual double visibleBeats() const = 0;
    virtual double drawnX() const = 0;         // the item's drawn x in the view, glide included
    virtual void stepTo(double elapsedMs) = 0; // advances a running glide (absolute ms since it began)
    virtual bool gliding() const = 0;
    virtual double pixelsPerBeat() const = 0;
};

// A Cutoff lane with one dot near the right edge of a 40 px/beat view.
class LaneDotSurface : public SelectionSurface {
public:
    enum class Gesture { keyHold, mouseEdgeDrag, movedThenScrolledAway };

    LaneDotSurface(Gesture gesture, bool animate)
        : gesture_(gesture) {
        bass_ = f_.doc.addTrack(synth::TrackKind::Midi, "Bass");
        lane_ = f_.addLane(bass_, "cutoff");
        f_.panel.setTrackAutomationExpanded(bass_, true);
        editor_ = f_.panel.laneEditorForTest(lane_);
        editor_->setTool(AutomationLaneEditor::Tool::Pointer);
        editor_->getKeyHoldForTest().host = keys_.host();
        editor_->forceGlideAnimateForTest(animate);
        start_ = std::floor(visibleBeats()) - 2.0;
        f_.doc.addBreakpoint(lane_, start_, 50.0);
        f_.undo.beginNewTransaction();
    }
    ~LaneDotSurface() override { editor_->forceGlideAnimateForTest(false); }

    void move() override {
        if (gesture_ == Gesture::mouseEdgeDrag)
            dragToTheEdge();
        else
            holdAltRight();
        editor_->getPointTweenForTest().stepForTest(5000.0);
        if (gesture_ == Gesture::movedThenScrolledAway) // the user scrolls on, past both places
            f_.panel.getViewState().firstVisibleBeat = itemBeat() + 2.0 * visibleBeats();
    }
    bool undo() override { return f_.undo.undo(); }
    bool redo() override { return f_.undo.redo(); }
    double itemBeat() const override { return f_.doc.getLane(lane_)->points.at(0).beat; }
    bool itemSelected() const override {
        const auto& selection = editor_->getPointSelection();
        return selection.contains(itemBeat()) && selection.getCursor() == itemBeat();
    }
    int selectedCount() const override { return editor_->getPointSelection().size(); }
    double firstVisible() const override { return f_.panel.getViewState().firstVisibleBeat; }
    double visibleBeats() const override { return (double)editor_->getWidth() / pixelsPerBeat(); }
    double drawnX() const override {
        const double beat = itemBeat();
        return f_.panel.getViewState().beatToX(beat) +
               (double)editor_->getPointTweenForTest().offsetFor(synth::ui::lane_key::tweenKey(beat)).x;
    }
    void stepTo(double elapsedMs) override { editor_->getPointTweenForTest().stepForTest(elapsedMs); }
    bool gliding() const override { return editor_->getPointTweenForTest().isAnimating(); }
    double pixelsPerBeat() const override { return f_.panel.getViewState().pixelsPerBeat; }

private:
    void holdAltRight() {
        editor_->getPointSelection().setSelection({start_});
        editor_->getPointSelection().setCursor(start_);
        keys_.down = {juce::KeyPress::rightKey};
        keys_.alt = true;
        editor_->keyPressed(
            juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys(juce::ModifierKeys::altModifier), 0));
        for (double ms = 0.0; ms <= 3000.0; ms += 16.0) {
            keys_.now = 1000.0 + synth::ui::lane_key::kTapMs + ms;
            editor_->getKeyHoldForTest().tick();
        }
        keys_.down.clear();
        editor_->getKeyHoldForTest().checkKeyUp();
    }
    void dragToTheEdge() {
        const auto& view = f_.panel.getViewState();
        const juce::Point<float> from{(float)view.beatToX(start_), (float)editor_->valueToY(50.0)};
        const juce::Point<float> edge{(float)editor_->getWidth() - 2.0f, from.y};
        editor_->mouseDown(makeClickEvent(*editor_, from, leftButton()));
        editor_->mouseDrag(makeDragEvent(*editor_, edge, from, leftButton()));
        for (int i = 0; i < 40 && editor_->isEdgeScrollRunningForTest(); ++i)
            editor_->tickEdgeScrollForTest();
        editor_->mouseUp(makeDragEvent(*editor_, edge, from, leftButton()));
    }

    Gesture gesture_;
    mutable LanesPanel f_; // the panel's view-state getter is non-const
    key_glide_test::FakeKeys keys_;
    synth::TrackId bass_;
    synth::LaneId lane_;
    AutomationLaneEditor* editor_ = nullptr;
    double start_ = 0.0;
};

struct Entry {
    const char* name;
    std::function<std::unique_ptr<SelectionSurface>(bool animate)> make;
};

const std::vector<Entry>& surfaces() {
    using G = LaneDotSurface::Gesture;
    static const std::vector<Entry> entries{
        {"lane dot, held Alt+Right past the edge",
         [](bool a) { return std::make_unique<LaneDotSurface>(G::keyHold, a); }},
        {"lane dot, mouse drag parked at the edge",
         [](bool a) { return std::make_unique<LaneDotSurface>(G::mouseEdgeDrag, a); }},
        {"lane dot, moved, then the view scrolled away",
         [](bool a) { return std::make_unique<LaneDotSurface>(G::movedThenScrolledAway, a); }},
    };
    return entries;
}

bool inView(const SelectionSurface& s) {
    return s.itemBeat() >= s.firstVisible() && s.itemBeat() <= s.firstVisible() + s.visibleBeats();
}

// Steps the restore's glide to its end. Under Full motion, with the item on screen when the restore began, the view
// glides: it has not moved on the restore's own frame, and it moves over several frames, never all at once.
void settle(SelectionSurface& s, bool glides, double viewBefore) {
    if (glides)
        EXPECT_EQ(s.firstVisible(), viewBefore) << "the view has not jumped on the restore's own frame";
    int framesThatScrolled = 0;
    double lastFirst = s.firstVisible();
    for (int frame = 1; frame <= 120 && s.gliding(); ++frame) {
        s.stepTo(16.0 * frame);
        if (s.firstVisible() != lastFirst)
            ++framesThatScrolled;
        lastFirst = s.firstVisible();
    }
    if (glides && s.firstVisible() != viewBefore)
        EXPECT_GE(framesThatScrolled, 3) << "the view followed the dot over several frames";
    EXPECT_FALSE(s.gliding()) << "the restore settled";
}

void expectRestored(SelectionSurface& s, double beat, const char* what) {
    SCOPED_TRACE(what);
    EXPECT_EQ(s.itemBeat(), beat);
    EXPECT_TRUE(s.itemSelected()) << "the item is still selected, the keyboard cursor on it";
    EXPECT_EQ(s.selectedCount(), 1);
    EXPECT_TRUE(inView(s)) << "beat " << s.itemBeat() << " outside the view [" << s.firstVisible() << ", "
                           << s.firstVisible() + s.visibleBeats() << "]";
}

} // namespace

// Regression test for FRO1689: Cmd+Z of a lane dot moved past the edge put it back but dropped its selection and left
// the view where the move had taken it, so the dot was off screen and not selected.
TEST(UndoKeepsSelection, EveryMovedItemStaysSelectedAndInViewAfterUndoAndRedo) {
    for (const auto& entry : surfaces()) {
        for (const Motion motion : {Motion::full, Motion::reduced, Motion::off}) {
            SCOPED_TRACE(std::string(entry.name) + ", " + nameOf(motion));
            MotionScope scope(motion);
            auto s = entry.make(motion == Motion::full);
            const double start = s->itemBeat();
            const double viewAtStart = s->firstVisible();
            s->move();
            const double moved = s->itemBeat();
            ASSERT_GT(moved, viewAtStart + s->visibleBeats()) << "the move carried the item out of the starting view";
            ASSERT_TRUE(s->itemSelected());

            const bool full = motion == Motion::full;
            double viewBefore = s->firstVisible();
            bool glides = full && inView(*s);
            ASSERT_TRUE(s->undo());
            settle(*s, glides, viewBefore);
            expectRestored(*s, start, "undo");

            viewBefore = s->firstVisible();
            glides = full && inView(*s);
            ASSERT_TRUE(s->redo());
            settle(*s, glides, viewBefore);
            expectRestored(*s, moved, "redo");
        }
    }
}

// An undo whose item is already in view leaves the view alone.
TEST(UndoKeepsSelection, AnUndoThatEndsInViewDoesNotScroll) {
    MotionScope scope(Motion::off);
    LanesPanel f;
    const auto bass = f.doc.addTrack(synth::TrackKind::Midi, "Bass");
    const auto lane = f.addLane(bass, "cutoff");
    f.panel.setTrackAutomationExpanded(bass, true);
    auto* editor = f.panel.laneEditorForTest(lane);
    editor->setTool(AutomationLaneEditor::Tool::Pointer);
    f.doc.addBreakpoint(lane, 4.0, 50.0);
    editor->getPointSelection().setSelection({4.0});
    editor->getPointSelection().setCursor(4.0);
    key_glide_test::FakeKeys keys; // no key physically down: the press is one committed step
    editor->getKeyHoldForTest().host = keys.host();
    f.undo.beginNewTransaction();
    ASSERT_TRUE(editor->keyPressed(
        juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys(juce::ModifierKeys::altModifier), 0)));
    ASSERT_EQ(f.doc.getLane(lane)->points[0].beat, 5.0);
    f.panel.getViewState().firstVisibleBeat = 1.0;

    ASSERT_TRUE(f.undo.undo());
    EXPECT_EQ(f.panel.getViewState().firstVisibleBeat, 1.0) << "beat 4 is in view; nothing scrolls";
    EXPECT_TRUE(editor->getPointSelection().contains(4.0));
    EXPECT_EQ(editor->getPointSelection().getCursor(), std::optional<double>(4.0));
}

} // namespace undo_keeps_selection
