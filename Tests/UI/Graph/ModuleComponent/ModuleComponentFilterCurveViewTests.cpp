// The Filter card's small curve view of the shape under the morph cursor: on a dot it shows and edits that shape
// (one undo step per drag), between dots it shows the blend and refuses edits, and the Show Curve pill is remembered.
#include "../../ModuleViews/CurveEditor/CurveEditorSurfaces.h"
#include "../../ModuleViews/ShapeEditor/ShapeEditorTestHelpers.h"
#include "ModuleComponentTestFixture.h"
#include "Modules/FilterModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Graph/ModuleComponent/MorphShapesCard/MorphShapesCard.h"
#include "UI/Layout/FadeAmount.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/ModuleViews/CurveEditor/CurveWaveConversion.h"
#include <algorithm>
#include <cmath>

using namespace synth::ui;
using namespace synth::ui::test;
using synth::CustomFilterShapes;

namespace {

struct CurveCardRig {
    AudioEngine engine;
    FilterModule* filter = nullptr;
    juce::AudioProcessorGraph::Node::Ptr node;
    GraphEditor editor{engine};
    AppUndoManager undo;
    std::unique_ptr<ModuleComponent> card;

    CurveCardRig() {
        filter = new FilterModule();
        node = engine.getGraph().addNode(std::unique_ptr<juce::AudioProcessor>(filter));
        *dynamic_cast<juce::AudioParameterChoice*>(findParameterByID(filter, "filterType")) =
            FilterModule::kCustomTypeIndex;
        card = std::make_unique<ModuleComponent>(filter, node->nodeID, editor, &undo);
        card->timerCallback();
    }

    MorphShapesCard& custom() { return *card->getMorphCard(); }
    CurveEditorComponent& curve() { return custom().getCurveView(); }

    void moveCursor(float x, float y) {
        *dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(filter, "morphX")) = x;
        *dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(filter, "morphY")) = y;
        card->timerCallback();
    }
};

} // namespace

TEST_F(ModuleComponentTest, CurveViewIsShownByDefaultWithSeveralShapes) {
    CurveCardRig rig;
    EXPECT_TRUE(rig.curve().isVisible());
    EXPECT_TRUE(rig.custom().getCurveToggle().getToggleState());
    EXPECT_EQ(rig.custom().getCurveToggle().getButtonText(), "Show Curve");
}

// Regression test for FRO1334: the Custom card had no way to see the shape under the morph cursor.
TEST_F(ModuleComponentTest, CurveViewShowsTheShapeTheCursorIsOn) {
    CurveCardRig rig;
    const auto defaults = CustomFilterShapes::defaultShapes();
    for (int i = 0; i < 3; ++i) {
        rig.moveCursor(defaults.shapes[(size_t)i].position[0], defaults.shapes[(size_t)i].position[1]);
        EXPECT_EQ(rig.custom().curveViewShape(), i);
        EXPECT_EQ(rig.curve().getModel().getNumNodes(), (int)defaults.shapes[(size_t)i].curve.points.size());
        EXPECT_TRUE(rig.curve().getTitle().isNotEmpty());
        EXPECT_NE(rig.curve().getDescription().indexOf(defaults.shapes[(size_t)i].name), -1);
    }
}

TEST_F(ModuleComponentTest, CurveViewBetweenDotsShowsTheBlendAndRefusesEdits) {
    CurveCardRig rig;
    rig.moveCursor(0.5f, 0.5f);
    EXPECT_EQ(rig.custom().curveViewShape(), -1);
    EXPECT_NE(rig.curve().getDescription().indexOf("read only"), -1);
    bool self = true;
    bool children = true;
    rig.curve().getInterceptsMouseClicks(self, children);
    EXPECT_FALSE(self);

    const int generation = rig.filter->getCustomShapesGeneration();
    const auto before = rig.filter->getCustomShapes();
    rig.curve().dragNodeTo(3, {0.5f, 0.9f});
    rig.curve().onGestureEnd();
    EXPECT_EQ(rig.filter->getCustomShapesGeneration(), generation);
    EXPECT_TRUE(rig.filter->getCustomShapes() == before);
}

TEST_F(ModuleComponentTest, DraggingAPointInTheCurveViewIsOneUndoStepAndTheSoundFollows) {
    CurveCardRig rig;
    const auto defaults = CustomFilterShapes::defaultShapes();
    rig.moveCursor(defaults.shapes[1].position[0], defaults.shapes[1].position[1]);
    ASSERT_EQ(rig.custom().curveViewShape(), 1);
    auto& curve = rig.curve();
    ASSERT_GT(curve.getWidth(), 0);
    const CurveEditorGeometry geometry(curve.getModel(), curve.plotBounds(), [] {
        CurveGeometryConfig config;
        config.explicitVisibleRange = 1.0;
        config.zeroSegmentPx = 0.0f;
        return config;
    }());
    const int node = 3;
    const auto from = geometry.nodePosition(node);
    const auto to = from + juce::Point<float>(0.0f, -20.0f);
    const int serial = rig.undo.getEditSerial();
    const int generation = rig.filter->getCustomShapesGeneration();

    curve.mouseDown(curveLeftClick(curve, from));
    curve.mouseDrag(curveLeftDrag(curve, to, from));
    EXPECT_GT(rig.filter->getCustomShapesGeneration(), generation) << "the module has the edit mid-drag";
    curve.mouseUp(curveLeftDrag(curve, to, from));

    EXPECT_GT(rig.filter->getCustomShapes().shapes[1].curve.points[(size_t)node].y,
              defaults.shapes[1].curve.points[(size_t)node].y);
    EXPECT_TRUE(rig.filter->getCustomShapes().shapes[0] == defaults.shapes[0]);
    EXPECT_EQ(rig.undo.getEditSerial(), serial + 1) << "a whole drag is one undo step";

    rig.undo.undo();
    EXPECT_TRUE(rig.filter->getCustomShapes().isDefault());
    rig.card->timerCallback();
    EXPECT_EQ(rig.curve().getModel().getNode(node).y, defaults.shapes[1].curve.points[(size_t)node].y)
        << "the view follows an undo";
}

TEST_F(ModuleComponentTest, ShowCurveToggleHidesTheViewClosesUpTheCardAndSurvivesASaveLoad) {
    CurveCardRig rig;
    const int shownHeight = rig.card->getHeight();
    auto& toggle = rig.custom().getCurveToggle();
    toggle.setToggleState(false, juce::sendNotificationSync);
    EXPECT_FALSE(rig.curve().isVisible());
    EXPECT_LT(rig.card->getHeight(), shownHeight);
    ASSERT_TRUE(rig.filter->getCardViewState().showCurveView.has_value());
    EXPECT_FALSE(*rig.filter->getCardViewState().showCurveView);

    const auto restored = synth::CardViewState::fromVar(rig.filter->getCardViewState().toVar());
    ASSERT_TRUE(restored.showCurveView.has_value());
    EXPECT_FALSE(*restored.showCurveView);

    ModuleComponent again(rig.filter, rig.node->nodeID, rig.editor, &rig.undo);
    again.timerCallback();
    EXPECT_FALSE(again.getMorphCard()->getCurveView().isVisible()) << "a rebuilt card keeps the choice";
}

// Part two of this file, the curve editor guards -- the guard for the bug kind "snap-misses-line": with Snap on, a dot
// dropped on a grid line sits exactly on it and the grid itself never moves. Every surface that owns a curve editor is
// driven the way a hand does it (press the dot, drag, release): one dot onto a line, then a second dot onto the same
// line, at the same x and at another, on every division the grid offers. J, the Snap key, flips the curve window's Snap
// and grid. After each drop the dot's pixel is the line's pixel and the set of lines is the one it was before. A new
// surface that owns a curve editor fails the registry test until it is added. (They live here so the test binary gains
// no translation unit that reaches the module headers.)

namespace curve_editor_guards {

namespace {

constexpr float kPixelTolerance = 0.01f;
const juce::KeyPress kJ('j', juce::ModifierKeys::noModifiers, 0);

struct Lines {
    std::vector<float> x; ///< pixel of each vertical line, left to right
    std::vector<float> y; ///< pixel of each horizontal line, level 0 to 1
    bool operator==(const Lines& o) const { return x == o.x && y == o.y; }
};

Lines linesOf(CurveEditorComponent& editor, int xDiv, int yDiv) {
    const CurveEditorGeometry geometry = editor.getGeometryForTest();
    const auto& model = editor.getModel();
    Lines lines;
    for (int k = 0; k <= xDiv; ++k)
        lines.x.push_back(geometry.xForTime(model.getMinX() + (model.getMaxX() - model.getMinX()) * k / xDiv));
    for (int i = 0; i <= yDiv; ++i)
        lines.y.push_back(geometry.yForLevel((float)i / (float)yDiv));
    return lines;
}

std::vector<int> interiorNodes(const CurveModel& model) {
    std::vector<int> out;
    for (int i = 0; i < model.getNumNodes(); ++i)
        if (model.getNode(i).xMovable && model.getNode(i).yMovable)
            out.push_back(i);
    return out;
}

void pressDragRelease(CurveEditorComponent& ed, juce::Point<float> from, juce::Point<float> to) {
    ed.mouseDown(curveLeftClick(ed, from));
    // Option held: the second dot is dropped on its neighbour's column, which a plain drag stops short of.
    ed.mouseDrag(curveLeftDragPass(ed, to, from));
    ed.mouseUp(curveLeftClick(ed, to));
}

/** Drags the interior dot `which` (0 = first, 1 = second, in x order) to the crossing of x line `col` and y line
 *  `row`, then checks the dot sits on it and the lines did not move. */
void dropOn(CurveEditorComponent& ed, const juce::String& what, int which, int col, int row, int xDiv, int yDiv,
            const Lines& expected) {
    const auto interior = interiorNodes(ed.getModel());
    ASSERT_GT((int)interior.size(), which) << what;
    const auto geometry = ed.getGeometryForTest();
    const auto from = geometry.nodePosition(interior[(size_t)which]);
    const juce::Point<float> to{expected.x[(size_t)col], expected.y[(size_t)row]};
    pressDragRelease(ed, from, to);

    const auto after = linesOf(ed, xDiv, yDiv);
    ASSERT_EQ(after.x.size(), expected.x.size());
    for (size_t k = 0; k < after.x.size(); ++k)
        EXPECT_NEAR(after.x[k], expected.x[k], kPixelTolerance)
            << what << ": vertical line " << k << " moved after dropping dot " << which << " on col " << col << " row "
            << row << " of " << yDiv;
    for (size_t i = 0; i < after.y.size(); ++i)
        EXPECT_NEAR(after.y[i], expected.y[i], kPixelTolerance) << what << ": horizontal line " << i << " moved";

    // The dot that was dropped (the one nearest the aim) is on the line: its drawn pixel is the line's pixel.
    const auto now = ed.getGeometryForTest();
    bool found = false;
    for (const int i : interiorNodes(ed.getModel())) {
        const auto pos = now.nodePosition(i);
        if (std::abs(pos.x - to.x) < kPixelTolerance && std::abs(pos.y - to.y) < kPixelTolerance)
            found = true;
    }
    EXPECT_TRUE(found) << what << ": no dot sits exactly on col " << col << " row " << row << " of " << yDiv
                       << " after dropping dot " << which << " there";
}

/** Two interior dots, placed freely (Shift) so the editor starts from a model every surface can hold. */
void addTwoDots(CurveEditorComponent& ed) {
    ed.setGrid(std::nullopt);
    ed.setSnapToGrid(false);
    ed.setModel(ed.getModel());
    const auto geometry = ed.getGeometryForTest();
    const auto& model = ed.getModel();
    const float left = geometry.xForTime(model.getMinX() + (model.getMaxX() - model.getMinX()) * 0.31);
    ed.addPointAt({left, geometry.yForLevel(0.37f)}, true);
    // The geometry is a view onto the live model, so it is taken again once a dot has been added.
    const auto again = ed.getGeometryForTest();
    const float right = again.xForTime(model.getMinX() + (model.getMaxX() - model.getMinX()) * 0.66);
    ed.addPointAt({right, again.yForLevel(0.81f)}, true);
}

/** The model a surface starts on, restored before each round so every round begins the same. */
class RestoringEditor {
public:
    explicit RestoringEditor(CurveEditorComponent& ed)
        : ed_(ed)
        , initial_(ed.getModel()) {}
    CurveEditorComponent& get() { return ed_; }
    void reset() { ed_.setModel(initial_); }

private:
    CurveEditorComponent& ed_;
    CurveModel initial_;
};

void sweepSurface(CurveEditorComponent& ed, const juce::String& what) {
    RestoringEditor surface(ed);
    for (const int divisions : {2, 4, 8, 16, 32}) {
        const int xDiv = juce::jmin(divisions, 8);
        for (int row = 0; row <= divisions; ++row) {
            for (const bool sameColumn : {true, false}) {
                surface.reset();
                addTwoDots(ed);
                ed.setGrid(CurveEditorComponent::CurveGrid{xDiv, divisions});
                ed.setSnapToGrid(true);
                const juce::String label = what + " 1/" + juce::String(divisions);
                const auto expected = linesOf(ed, xDiv, divisions);
                const int col =
                    1 + (row %
                         juce::jmax(
                             1, xDiv - 3)); // clear of the end dots' columns, with room for the other dot to its right
                const int otherCol =
                    xDiv < 4 ? col
                             : col + 1; // right of the first dot, so the two keep their order; 1/2 has one inner column
                dropOn(ed, label, 0, col, row, xDiv, divisions, expected);
                dropOn(ed, label, 1, sameColumn ? col : otherCol, row, xDiv, divisions, expected);
                // A dense curve (the shape windows start with many dots) has other dots to drag first; the two dots
                // are the whole curve's interior only on a sparse one.
                if (sameColumn || otherCol == col || interiorNodes(ed.getModel()).size() != 2)
                    continue; // two dots on one x may be refused by a dense curve; the grid check above is the guard
                const auto geometry = ed.getGeometryForTest();
                int onRow = 0;
                for (const int i : interiorNodes(ed.getModel()))
                    if (std::abs(geometry.nodePosition(i).y - expected.y[(size_t)row]) < kPixelTolerance)
                        ++onRow;
                EXPECT_GE(onRow, 2) << label << ": both dots sit on the same line (row " << row << ")";
            }
        }
    }
}

bool onSomeLine(const std::vector<float>& lines, float pixel) {
    for (const float line : lines)
        if (std::abs(line - pixel) < kPixelTolerance)
            return true;
    return false;
}

/** Alt+arrows with Snap on: from a dot on a line, every step lands on a line, and the grid stays. */
void sweepKeys(CurveEditorComponent& ed, const juce::String& what) {
    // Only the mod curve box and window take their grid from outside (the Snap button and the Grid box). The shape
    // windows and the cards own theirs (the Grid and Level rows, the LFO's note-length box rebuild it after every
    // edit), so a grid set from outside does not hold across a key step; their keys are covered with the rows' own
    // lines in the shape window grid tests.
    if (!what.contains("mod curve"))
        return;
    RestoringEditor surface(ed);
    for (const int divisions : {4, 8, 16}) {
        const int xDiv = juce::jmin(divisions, 8);
        surface.reset();
        addTwoDots(ed);
        ed.setGrid(CurveEditorComponent::CurveGrid{xDiv, divisions});
        ed.setSnapToGrid(true);
        const juce::String label = what + " keys 1/" + juce::String(divisions);
        const auto expected = linesOf(ed, xDiv, divisions);
        dropOn(ed, label, 0, 2, divisions / 2, xDiv, divisions, expected);
        dropOn(ed, label, 1, 3, divisions / 4, xDiv, divisions, expected); // every dot starts on a line
        const auto interior = interiorNodes(ed.getModel());
        ASSERT_FALSE(interior.empty()) << label;
        const auto at = ed.getGeometryForTest().nodePosition(interior.front());
        ed.mouseDown(curveLeftClick(ed, at)); // a click selects the dot
        ed.mouseUp(curveLeftClick(ed, at));
        for (const int code : {juce::KeyPress::upKey, juce::KeyPress::upKey, juce::KeyPress::downKey,
                               juce::KeyPress::leftKey, juce::KeyPress::rightKey, juce::KeyPress::rightKey}) {
            ed.keyPressed(juce::KeyPress(code, juce::ModifierKeys::altModifier, 0));
            const auto after = linesOf(ed, xDiv, divisions);
            ASSERT_EQ(after.x.size(), expected.x.size());
            for (size_t k = 0; k < after.x.size(); ++k)
                EXPECT_NEAR(after.x[k], expected.x[k], kPixelTolerance)
                    << label << ": Alt+arrow " << code << " moved vertical line " << k;
            for (size_t k = 0; k < after.y.size(); ++k)
                EXPECT_NEAR(after.y[k], expected.y[k], kPixelTolerance)
                    << label << ": Alt+arrow " << code << " moved horizontal line " << k;
            const auto now = ed.getGeometryForTest();
            for (const int i : interiorNodes(ed.getModel())) {
                const auto pos = now.nodePosition(i);
                if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
                    EXPECT_TRUE(onSomeLine(expected.y, pos.y))
                        << label << ": key " << code << " left the dot off the y lines: y " << pos.y << ", lines "
                        << expected.y[0] << ".." << expected.y.back() << " step "
                        << (expected.y.size() > 1 ? expected.y[1] - expected.y[0] : 0.0f);
                else
                    EXPECT_TRUE(onSomeLine(expected.x, pos.x)) << label << ": a key step left the dot off the x lines";
            }
        }
    }
}

} // namespace

TEST_F(ModuleComponentTest, AltArrowsWithSnapOnLandOnLinesOnEveryCurveEditorSurface) {
    forEachCurveEditorSurface([](CurveEditorComponent& ed, const juce::String& what) { sweepKeys(ed, what); });
}

TEST_F(ModuleComponentTest, JFlipsTheCurveWindowsSnapButtonAndGridAndSaysSoWithoutReachingTheApp) {
    BoxRig rig;
    ModCurveWindowContent window(rig.target);
    window.setSize(520, 440);
    int relayed = 0;
    window.onAppShortcut = [&](const juce::KeyPress&) {
        ++relayed;
        return false;
    };
    auto& editor = window.view().editor();
    const bool before = window.snapButton().getToggleState();
    EXPECT_EQ(editor.isSnapToGrid(), before);

    EXPECT_TRUE(window.keyPressed(kJ));
    EXPECT_EQ(window.snapButton().getToggleState(), !before) << "the button lights";
    EXPECT_EQ(editor.isSnapToGrid(), !before) << "the grid follows";
    EXPECT_EQ(window.shownHint(), WindowSnapKey::message(!before));

    EXPECT_TRUE(window.keyPressed(kJ));
    EXPECT_EQ(window.snapButton().getToggleState(), before);
    EXPECT_EQ(editor.isSnapToGrid(), before);
    EXPECT_EQ(window.shownHint(), WindowSnapKey::message(before));
    EXPECT_EQ(relayed, 0) << "J stays in the window: the timeline's Snap is not flipped";

    window.keyPressed(juce::KeyPress('k', juce::ModifierKeys::noModifiers, 0));
    EXPECT_EQ(relayed, 1) << "other keys still reach the app";
    EXPECT_TRUE(window.snapButton().getTooltip().containsIgnoreCase("(j)")) << window.snapButton().getTooltip();
}

namespace {
struct HintFadeMotion {
    HintFadeMotion() {
        synth::ui::setAnimationMode(synth::ui::AnimationMode::full);
        synth::ui::setReducedMotionForTest(false);
        FadeVisibility::setAnimateOffScreenForTest(true);
    }
    ~HintFadeMotion() {
        FadeVisibility::setAnimateOffScreenForTest(false);
        synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem);
        synth::ui::setReducedMotionForTest(std::nullopt);
    }
};

float visibleHintAlpha(const ModCurveWindowContent::HintAlphas& a) { return a.hint + a.message + a.previous; }
} // namespace

// Regression test for FRO1990: the hint was replaced by "Snap on" at once and swapped back at once, 1.8 s later.
TEST_F(ModuleComponentTest, TheSnapKeyMessageCrossFadesWithTheHintAndWithTheMessageItReplaces) {
    HintFadeMotion motion;
    BoxRig rig;
    ModCurveWindowContent window(rig.target);
    window.setSize(520, 440);
    const juce::KeyPress j('j', juce::ModifierKeys::noModifiers, 0);
    EXPECT_FLOAT_EQ(window.hintAlphas().hint, 1.0f);

    ASSERT_TRUE(window.keyPressed(j));
    EXPECT_FLOAT_EQ(window.hintAlphas().hint, 1.0f) << "the first frame still shows the hint";
    for (const float t : {0.25f, 0.5f, 0.75f}) {
        FadeAmount::stepAllForTest(t);
        const auto a = window.hintAlphas();
        EXPECT_NEAR(a.hint, 1.0f - t, 1e-4f) << "the hint fades out as the message fades in at " << t;
        EXPECT_NEAR(a.message, t, 1e-4f);
        EXPECT_NEAR(visibleHintAlpha(a), 1.0f, 1e-4f) << "something is always drawn";
    }
    FadeAmount::stepAllForTest(1.0f);
    EXPECT_NEAR(window.hintAlphas().message, 1.0f, 1e-4f);
    const auto first = window.shownHint();

    ASSERT_TRUE(window.keyPressed(j)); // Snap on -> Snap off
    EXPECT_NE(window.shownHint(), first);
    for (const float t : {0.0f, 0.25f, 0.5f, 0.75f}) {
        FadeAmount::stepAllForTest(t);
        const auto a = window.hintAlphas();
        EXPECT_NEAR(a.message, t, 1e-4f) << "the new message rises at " << t;
        EXPECT_NEAR(a.previous, 1.0f - t, 1e-4f) << "the old one falls";
        EXPECT_NEAR(a.hint, 0.0f, 1e-4f);
        EXPECT_NEAR(visibleHintAlpha(a), 1.0f, 1e-4f) << "the line never goes empty";
    }
    FadeAmount::stepAllForTest(1.0f);

    window.endSnapMessageForTest();
    EXPECT_EQ(window.shownHint(), ModCurveWindowContent::hintText()) << "the logical text is the hint again at once";
    for (const float t : {0.25f, 0.5f, 0.75f}) {
        FadeAmount::stepAllForTest(t);
        const auto a = window.hintAlphas();
        EXPECT_NEAR(a.hint, t, 1e-4f) << "the hint returns as the message falls at " << t;
        EXPECT_NEAR(visibleHintAlpha(a), 1.0f, 1e-4f);
    }
    FadeAmount::stepAllForTest(1.0f);
    EXPECT_FLOAT_EQ(window.hintAlphas().hint, 1.0f);
    EXPECT_FLOAT_EQ(window.hintAlphas().message, 0.0f);
}

TEST_F(ModuleComponentTest, SnapOnEveryCurveEditorSurfaceKeepsTheGridAndLandsOnLines) {
    forEachCurveEditorSurface([](CurveEditorComponent& ed, const juce::String& what) { sweepSurface(ed, what); });
}

TEST_F(ModuleComponentTest, EveryCurveEditorOwnerIsInTheSurfaceList) {
    expectEveryCurveEditorOwnerIsListed("Tests/UI/Graph/ModuleComponent/ModuleComponentFilterCurveViewTests.cpp");
}

// Part two of this file, the guard for "motion-over-time" on edits: a dot added or deleted inside a curve editor eases
// the line into its new shape, the way undo and redo already do (docs/layout/undo-glide.md, "Add and delete"). Every
// surface that owns a curve editor is walked (CurveEditorSurfaces.h) and every way to delete (Delete key, double-click
// on the dot, the right-click menu's removeNode) and to add (double-click on empty space) is driven: the first frame is
// the old line, a middle frame is between the old and the new line, and it settles on the new one; a second edit
// mid-glide continues from where the line is drawn; Reduce Motion lands the line and fades it in; Animations Off lands
// it at once.

namespace editglide {

struct Motion {
    explicit Motion(AnimationMode mode, bool reduced = false) {
        setAnimationMode(mode);
        setReducedMotionForTest(reduced);
    }
    ~Motion() {
        setAnimationMode(AnimationMode::followSystem);
        setReducedMotionForTest(std::nullopt);
    }
};

using Line = std::vector<juce::Point<float>>;

float lineYAt(const Line& line, float x) {
    for (size_t i = 1; i < line.size(); ++i)
        if (line[i].x >= x) {
            const float span = line[i].x - line[i - 1].x;
            return span > 0.0f ? line[i - 1].y + (line[i].y - line[i - 1].y) * (x - line[i - 1].x) / span : line[i].y;
        }
    return line.empty() ? 0.0f : line.back().y;
}

/** The largest vertical gap between two lines over the width they share. */
float gap(const Line& a, const Line& b) {
    float worst = 0.0f;
    for (int k = 0; k <= 60; ++k) {
        const float x = a.front().x + (a.back().x - a.front().x) * (float)k / 60.0f;
        worst = std::max(worst, std::abs(lineYAt(a, x) - lineYAt(b, x)));
    }
    return worst;
}

Line lineOf(CurveEditorComponent& ed) { return ed.getDrawnCurvePointsForTest(); }

void armTweens(CurveEditorComponent& ed) {
    ed.getPointTweenForTest().forceAnimateForTest(true);
    ed.getBendTweenForTest().forceAnimateForTest(true);
    ed.getMorphTweenForTest().forceAnimateForTest(true);
}

void step(CurveEditorComponent& ed, double ms) {
    ed.getPointTweenForTest().stepForTest(ms);
    ed.getBendTweenForTest().stepForTest(ms);
    ed.getMorphTweenForTest().stepForTest(ms);
}

bool gliding(CurveEditorComponent& ed) {
    return ed.getPointTweenForTest().isAnimating() || ed.getBendTweenForTest().isAnimating() ||
           ed.getMorphTweenForTest().isAnimating();
}

void settle(CurveEditorComponent& ed) {
    ed.getPointTweenForTest().completeForTest();
    ed.getBendTweenForTest().completeForTest();
    ed.getMorphTweenForTest().completeForTest();
}

/** Free placement of dots at fixed fractions, until the curve holds three interior dots, then settled. */
void prepare(CurveEditorComponent& ed, const CurveModel& initial) {
    armTweens(ed);
    ed.setModel(initial);
    settle(ed);
    ed.setGrid(std::nullopt);
    ed.setSnapToGrid(false);
    for (const auto [fx, level] : {std::pair{0.25f, 0.8f}, {0.5f, 0.2f}, {0.75f, 0.7f}}) {
        const auto geometry = ed.getGeometryForTest();
        const auto& model = ed.getModel();
        const double t = model.getMinX() + (model.getMaxX() - model.getMinX()) * fx;
        ed.addPointAt({geometry.xForTime(t), geometry.yForLevel(level)}, true);
    }
    settle(ed);
}

/** The interior dots, the ones whose removal changes the line most first: a dot on the straight line between its
 *  neighbours would leave the line as it was. */
std::vector<int> interior(CurveEditorComponent& ed) {
    const auto& model = ed.getModel();
    const auto geometry = ed.getGeometryForTest();
    std::vector<std::pair<float, int>> ranked;
    for (int i = 1; i + 1 < model.getNumNodes(); ++i) {
        if (!model.getNode(i).xMovable)
            continue;
        const auto a = geometry.nodePosition(i - 1);
        const auto b = geometry.nodePosition(i);
        const auto c = geometry.nodePosition(i + 1);
        const float span = c.x - a.x;
        const float onChord = span > 0.0f ? a.y + (c.y - a.y) * (b.x - a.x) / span : a.y;
        ranked.emplace_back(std::abs(b.y - onChord), i);
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& l, const auto& r) { return l.first > r.first; });
    std::vector<int> out;
    for (const auto& entry : ranked)
        out.push_back(entry.second);
    return out;
}

enum class Way { deleteKey, doubleClickDot, menuDelete, doubleClickEmpty };
const Way kWays[] = {Way::deleteKey, Way::doubleClickDot, Way::menuDelete, Way::doubleClickEmpty};

const char* nameOf(Way way) {
    switch (way) {
    case Way::deleteKey:
        return "Delete key";
    case Way::doubleClickDot:
        return "double-click on a dot";
    case Way::menuDelete:
        return "Delete Point (menu)";
    case Way::doubleClickEmpty:
        return "double-click on empty space";
    }
    return "";
}

/** Does one edit by `way` on the curve's `which`-th interior dot (or, for an add, on empty space). */
void edit(CurveEditorComponent& ed, Way way, int which) {
    const auto nodes = interior(ed);
    const int index = nodes[(size_t)std::min<int>(which, (int)nodes.size() - 1)];
    const auto geometry = ed.getGeometryForTest();
    const auto pos = geometry.nodePosition(index);
    switch (way) {
    case Way::deleteKey:
        ed.mouseDown(curveLeftClick(ed, pos)); // a real click selects the dot
        ed.mouseUp(curveLeftClick(ed, pos));
        ed.keyPressed(juce::KeyPress(juce::KeyPress::deleteKey));
        break;
    case Way::doubleClickDot:
        ed.mouseDoubleClick(curveDoubleClick(ed, pos));
        break;
    case Way::menuDelete:
        ed.removeNode(index);
        break;
    case Way::doubleClickEmpty: {
        // Off the line (a dot on it would leave the line as it was), in the widest gap between two dots, where nothing
        // is hit.
        const auto line = lineOf(ed);
        size_t widest = 1;
        for (size_t i = 1; i < line.size(); ++i)
            if (line[i].x - line[i - 1].x > line[widest].x - line[widest - 1].x)
                widest = i;
        juce::Point<float> at;
        for (float fraction = 0.15f; fraction < 0.9f; fraction += 0.05f) {
            const float x = line[widest - 1].x + (line[widest].x - line[widest - 1].x) * fraction;
            const float onLine = lineYAt(line, x);
            at = {x, onLine + (onLine < (float)ed.getHeight() * 0.5f ? 45.0f : -45.0f)};
            if (ed.hitTest(at).kind == CurveHitKind::None)
                break; // clear of the dots and the bend rings
        }
        ed.mouseDoubleClick(curveDoubleClick(ed, at));
        break;
    }
    }
}

bool isAdd(Way way) { return way == Way::doubleClickEmpty; }

/** The Filter card's view opens the big editor on a double-click in empty space, so the add is not one of its ways. */
bool hasWay(const juce::String& surface, Way way) {
    return !(way == Way::doubleClickEmpty && surface == "Filter card curve");
}

} // namespace editglide
using namespace editglide;

TEST_F(ModuleComponentTest, AnAddedOrDeletedDotEasesTheLineOnEverySurfaceAndEveryWay) {
    forEachCurveEditorSurface([](CurveEditorComponent& surface, const juce::String& what) {
        const CurveModel initial = surface.getModel();
        for (const Way way : kWays) {
            if (!hasWay(what, way))
                continue;
            SCOPED_TRACE(what + ": " + nameOf(way));
            Motion motion(AnimationMode::full);
            prepare(surface, initial);
            const int before = surface.getModel().getNumNodes();
            const Line old = lineOf(surface);

            edit(surface, way, 0);
            ASSERT_EQ(surface.getModel().getNumNodes(), before + (isAdd(way) ? 1 : -1));
            ASSERT_TRUE(gliding(surface)) << "the edit starts a glide";
            const Line first = lineOf(surface);
            EXPECT_LT(gap(first, old), 1.0f) << "the first frame is still the old line";

            step(surface, 70.0);
            const Line mid = lineOf(surface);
            step(surface, 1000.0);
            EXPECT_FALSE(gliding(surface));
            const Line fin = lineOf(surface);
            ASSERT_GT(gap(old, fin), 2.0f) << "the edit changed the line";
            EXPECT_GT(gap(mid, old), 0.3f) << "a middle frame has left the old line";
            EXPECT_GT(gap(mid, fin), 0.3f) << "and has not arrived";
            EXPECT_LT(gap(mid, old), gap(old, fin) + 1.0f);
            EXPECT_LT(gap(mid, fin), gap(old, fin) + 1.0f) << "a middle frame lies between the two lines";
        }
    });
}

TEST_F(ModuleComponentTest, ASecondEditMidGlideContinuesFromWhereTheLineIsDrawn) {
    forEachCurveEditorSurface([](CurveEditorComponent& surface, const juce::String& what) {
        const CurveModel initial = surface.getModel();
        SCOPED_TRACE(what);
        Motion motion(AnimationMode::full);
        prepare(surface, initial);
        edit(surface, Way::menuDelete, 0);
        step(surface, 70.0);
        const Line drawnNow = lineOf(surface);
        ASSERT_TRUE(gliding(surface));
        edit(surface, Way::menuDelete, 1);
        EXPECT_LT(gap(lineOf(surface), drawnNow), 1.0f) << "the second delete starts from the line as drawn";
        step(surface, 1000.0);
        EXPECT_FALSE(gliding(surface));
    });
}

TEST_F(ModuleComponentTest, UnderReduceMotionADeleteLandsTheLineAndFadesItInAndAnimationsOffLandsItAtOnce) {
    forEachCurveEditorSurface([](CurveEditorComponent& surface, const juce::String& what) {
        const CurveModel initial = surface.getModel();
        for (const Way way : kWays) {
            if (!hasWay(what, way))
                continue;
            SCOPED_TRACE(what + ": " + nameOf(way));
            {
                Motion motion(AnimationMode::reduced, true);
                prepare(surface, initial);
                edit(surface, way, 0);
                const Line landed = lineOf(surface);
                EXPECT_LT(surface.getMorphTweenForTest().alphaFor(0), 1.0f) << "the new line fades in";
                step(surface, 1000.0);
                EXPECT_NEAR(surface.getMorphTweenForTest().alphaFor(0), 1.0f, 1e-4f);
                EXPECT_LT(gap(landed, lineOf(surface)), 0.5f) << "the line did not move while it faded";
            }
            {
                Motion motion(AnimationMode::off);
                prepare(surface, initial);
                edit(surface, way, 0);
                EXPECT_FALSE(gliding(surface));
                EXPECT_NEAR(surface.getMorphTweenForTest().alphaFor(0), 1.0f, 1e-4f);
            }
        }
    });
}

} // namespace curve_editor_guards

namespace {
juce::MouseEvent clickAt(juce::Component& c, juce::Point<float> p) { return curveLeftClick(c, p); }

void clickDot(CurveCardRig& rig, int dot) {
    auto& map = rig.custom().getMap();
    map.mouseDown(clickAt(map, map.getDotCentre(dot)));
    map.mouseUp(clickAt(map, map.getDotCentre(dot)));
}
} // namespace

// Regression test for FRO1597: the view followed the cursor only; it now shows the selected dot.
TEST_F(ModuleComponentTest, CurveViewShowsTheSelectedDotWhileTheCursorSitsOnAnotherAndNamesIt) {
    CurveCardRig rig;
    const auto defaults = CustomFilterShapes::defaultShapes();
    rig.moveCursor(defaults.shapes[0].position[0], defaults.shapes[0].position[1]);
    ASSERT_EQ(rig.custom().curveViewShape(), 0);

    clickDot(rig, 2);
    EXPECT_EQ(rig.custom().curveViewShape(), 2);
    EXPECT_EQ(rig.curve().getModel().getNumNodes(), (int)defaults.shapes[2].curve.points.size());
    EXPECT_NE(rig.curve().getDescription().indexOf(defaults.shapes[2].name), -1) << "the AX text names the dot";
    EXPECT_EQ(rig.custom().getCurveView().getDescription().indexOf(defaults.shapes[0].name), -1);

    // A keyboard walk selects too: a dot's stop taking focus (Tab or an arrow walk) selects that dot.
    rig.custom().getMap().getDotStop(1)->focusGained(juce::Component::focusChangedByTabKey);
    EXPECT_EQ(rig.custom().curveViewShape(), 1) << "Tab to a dot re-reads the view";
}

TEST_F(ModuleComponentTest, DraggingInTheCurveViewWritesTheSelectedDotAndLeavesTheOneUnderTheCursor) {
    CurveCardRig rig;
    const auto defaults = CustomFilterShapes::defaultShapes();
    rig.moveCursor(defaults.shapes[0].position[0], defaults.shapes[0].position[1]);
    clickDot(rig, 2);
    auto& curve = rig.curve();
    const CurveEditorGeometry geometry(curve.getModel(), curve.plotBounds(), [] {
        CurveGeometryConfig config;
        config.explicitVisibleRange = 1.0;
        config.zeroSegmentPx = 0.0f;
        return config;
    }());
    const int node = 3;
    const auto from = geometry.nodePosition(node);
    const auto to = from + juce::Point<float>(0.0f, -20.0f);
    curve.mouseDown(curveLeftClick(curve, from));
    curve.mouseDrag(curveLeftDrag(curve, to, from));
    curve.mouseUp(curveLeftDrag(curve, to, from));

    const auto shapes = rig.filter->getCustomShapes().shapes;
    EXPECT_GT(shapes[2].curve.points[(size_t)node].y, defaults.shapes[2].curve.points[(size_t)node].y);
    EXPECT_TRUE(shapes[0] == defaults.shapes[0]) << "the dot under the cursor is untouched";
}

TEST_F(ModuleComponentTest, SelectingTheCursorAgainBringsBackTheViewThatFollowsTheCursor) {
    CurveCardRig rig;
    const auto defaults = CustomFilterShapes::defaultShapes();
    rig.moveCursor(0.5f, 0.5f);
    ASSERT_EQ(rig.custom().curveViewShape(), -1);
    clickDot(rig, 2);
    ASSERT_EQ(rig.custom().curveViewShape(), 2) << "a selected dot is editable although the cursor is between dots";

    auto& map = rig.custom().getMap();
    map.mouseDown(clickAt(map, map.getCursorCentre()));
    map.mouseUp(clickAt(map, map.getCursorCentre()));
    ASSERT_TRUE(map.isCursorSelected());
    EXPECT_EQ(rig.custom().curveViewShape(), -1) << "the cursor between dots: the read-only blend again";
    EXPECT_NE(rig.curve().getDescription().indexOf("read only"), -1);

    rig.moveCursor(defaults.shapes[1].position[0], defaults.shapes[1].position[1]);
    EXPECT_EQ(rig.custom().curveViewShape(), 1) << "and on a dot, that dot";
}
