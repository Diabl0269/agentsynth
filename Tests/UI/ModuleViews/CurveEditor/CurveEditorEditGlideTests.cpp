// CurveEditorEditGlideTests.cpp -- a dot added or deleted inside a curve editor eases the line into its new shape, the
// way undo and redo already do (docs/layout/undo-glide.md, "Add and delete"). Every surface that owns a curve editor
// is walked (CurveEditorSurfaces.h) and every way to delete (Delete key, double-click on the dot, the right-click
// menu's removeNode) and to add (double-click on empty space) is driven: the first frame is the old line, a middle
// frame is between the old and the new line, and it settles on the new one; a second edit mid-glide continues from
// where the line is drawn; Reduce Motion lands the line and fades it in; Animations Off lands it at once.
#include "CurveEditorSurfaces.h"
#include "UI/Layout/ReducedMotion.h"
#include <algorithm>
#include <cmath>

using namespace synth::ui;
using namespace synth::ui::test;

namespace {

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

} // namespace

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

TEST_F(ModuleComponentTest, EveryCurveEditorOwnerIsInTheSurfaceListForTheEditGlide) {
    expectEveryCurveEditorOwnerIsListed("Tests/UI/ModuleViews/CurveEditor/CurveEditorEditGlideTests.cpp");
}
