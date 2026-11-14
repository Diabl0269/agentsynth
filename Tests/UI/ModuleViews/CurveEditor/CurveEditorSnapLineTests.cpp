// CurveEditorSnapLineTests.cpp -- the guard for the bug kind "snap-misses-line": with Snap on, a dot dropped on a grid
// line sits exactly on it and the grid itself never moves. Every surface that owns a curve editor is driven the way a
// hand does it (press the dot, drag, release): one dot onto a line, then a second dot onto the same line, at the same x
// and at another, on every division the grid offers. After each drop the dot's pixel is the line's pixel and the set of
// lines is the one it was before. A new surface that owns a curve editor fails the registry test until it is added.
#include "CurveEditorSurfaces.h"
#include <cmath>

using namespace synth::ui;
using namespace synth::ui::test;

namespace {

constexpr float kPixelTolerance = 0.01f;

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
                const int col = 1 + (row % juce::jmax(1, xDiv - 1));
                const int otherCol = col < xDiv - 1 ? col + 1 : col - 1;
                dropOn(ed, label, 0, col, row, xDiv, divisions, expected);
                dropOn(ed, label, 1, sameColumn ? col : otherCol, row, xDiv, divisions, expected);
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

TEST_F(ModuleComponentTest, SnapOnEveryCurveEditorSurfaceKeepsTheGridAndLandsOnLines) {
    forEachCurveEditorSurface([](CurveEditorComponent& ed, const juce::String& what) { sweepSurface(ed, what); });
}

TEST_F(ModuleComponentTest, EveryCurveEditorOwnerIsInTheSurfaceList) {
    expectEveryCurveEditorOwnerIsListed("Tests/UI/ModuleViews/CurveEditor/CurveEditorSnapLineTests.cpp");
}
