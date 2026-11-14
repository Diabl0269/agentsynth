// CurveEditorSnapLineTests.cpp -- the guard for the bug kind "snap-misses-line": with Snap on, a dot dropped on a grid
// line sits exactly on it and the grid itself never moves. Every surface that owns a curve editor is driven the way a
// hand does it (press the dot, drag, release): one dot onto a line, then a second dot onto the same line, at the same x
// and at another, on every division the grid offers. J, the Snap key, flips the curve window's Snap and grid. After
// each drop the dot's pixel is the line's pixel and the set of lines is the one it was before. A new surface that owns
// a curve editor fails the registry test until it is added.
#include "CurveEditorSurfaces.h"
#include "UI/Layout/ReducedMotion.h"
#include <algorithm>
#include <cmath>

using namespace synth::ui;
using namespace synth::ui::test;

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
    ed.mouseDrag(curveLeftDrag(ed, to, from));
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
    const float right = geometry.xForTime(model.getMinX() + (model.getMaxX() - model.getMinX()) * 0.66);
    ed.addPointAt({left, geometry.yForLevel(0.37f)}, true);
    const auto again = ed.getGeometryForTest();
    ed.addPointAt({right - (geometry.xForTime(0.66) - again.xForTime(0.66)), again.yForLevel(0.81f)}, true);
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
                const int col = 1 + (row % juce::jmax(1, xDiv - 2)); // clear of the end dots' columns
                const int otherCol = xDiv < 4 ? col : (col < xDiv - 2 ? col + 1 : col - 1); // 1/2 has one inner column
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

TEST_F(ModuleComponentTest, SnapOnEveryCurveEditorSurfaceKeepsTheGridAndLandsOnLines) {
    forEachCurveEditorSurface([](CurveEditorComponent& ed, const juce::String& what) { sweepSurface(ed, what); });
}

TEST_F(ModuleComponentTest, EveryCurveEditorOwnerIsInTheSurfaceList) {
    expectEveryCurveEditorOwnerIsListed("Tests/UI/ModuleViews/CurveEditor/CurveEditorSnapLineTests.cpp");
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
