// PointEditorMatrixProbes.cpp -- the behaviour columns of the point editor matrix (docs/control/point-editing.md): each
// probe is one short script of real key and mouse events against the PointDriver of an editor.
#include "PointEditorMatrixProbes.h"
#include "UI/Layout/ReducedMotion.h"
#include <cmath>
#include <sstream>

namespace point_matrix {
namespace {

template <typename... Parts>
std::string said(const Parts&... parts) {
    std::ostringstream out;
    (out << ... << parts);
    return out.str();
}

std::string boxSelect(PointDriver& d) {
    d.selectPoint(2);
    d.box(0, 1);
    if (d.selected() != 2 || !d.selectedDot(0) || !d.selectedDot(1) || d.selectedDot(2))
        return said("a box over two dots left ", d.selected(), " selected");
    return "";
}

// The cursor a box leaves is the dot it took in last, from whichever side it grew, never always the far end: a group
// must not read as one hovered dot at one end.
std::string boxCursorLast(PointDriver& d) {
    const float top = 2.0f, bottom = (float)d.surface().getHeight() - 2.0f;
    const float x0 = d.dot(0).x, x1 = d.dot(1).x;
    d.boxWalk({x0 - 10.0f, top}, {x0 - 10.0f, bottom}, {x1 + 10.0f, bottom});
    if (d.selected() != 2)
        return said("a box walked rightward over two dots left ", d.selected(), " selected");
    if (d.selectedIndex() != 1)
        return said("a box grown from the left left the cursor on dot ", d.selectedIndex(),
                    ", not dot 1 it took in last");
    d.boxWalk({x1 + 10.0f, top}, {x1 + 10.0f, bottom}, {x0 - 10.0f, bottom});
    if (d.selected() != 2)
        return said("a box walked leftward over two dots left ", d.selected(), " selected");
    if (d.selectedIndex() != 0)
        return said("a box grown from the right left the cursor on dot ", d.selectedIndex(),
                    ", not dot 0 it took in last");
    return "";
}

// A click on one dot of a group selects just that dot, even when the pointer wobbles a pixel or two on the way (a click
// that jitters is still a click: no group move starts, no undo step opens).
std::string clickOneOfGroup(PointDriver& d) {
    d.box(0, 1);
    if (d.selected() != 2)
        return said("a box over two dots left ", d.selected(), " selected");
    const int steps = d.editSteps();
    d.wobblyClick(d.dot(1));
    if (d.selected() != 1 || !d.selectedDot(1))
        return said("a click with a 2 px wobble on one of two selected dots left ", d.selected(), " selected");
    if (d.editSteps() != steps)
        return "a wobbly click opened an undo step";
    return "";
}

std::string shiftExtend(PointDriver& d) {
    d.selectPoint(0);
    if (!d.key(juce::KeyPress::rightKey, kShift))
        return "Shift+Right was not taken";
    if (d.selected() != 2 || !d.selectedDot(0) || !d.selectedDot(1))
        return said("Shift+Right left ", d.selected(), " selected, not the first two");
    return "";
}

std::string altMove(PointDriver& d) {
    const int p = d.movePoint();
    d.selectPoint(p);
    const double x = d.position(p);
    int steps = d.editSteps();
    if (!d.moveKey(juce::KeyPress::rightKey) || !(d.position(p) > x))
        return "the move key did not move the selection along";
    if (d.editSteps() != steps + 1)
        return said("a move was ", d.editSteps() - steps, " undo steps");
    const double y = d.value(p);
    steps = d.editSteps();
    if (!d.moveKey(juce::KeyPress::upKey) || !(d.value(p) > y))
        return "Up did not raise the value";
    if (d.editSteps() != steps + 1)
        return said("a value move was ", d.editSteps() - steps, " undo steps");
    const double raised = d.value(p);
    steps = d.editSteps();
    if (!d.moveKey(juce::KeyPress::downKey) || !(d.value(p) < raised))
        return "Down did not lower the value";
    return d.editSteps() == steps + 1 ? "" : "a value move was not one undo step";
}

// Holds the move key on the selected point: the hold is live, the point glides past its tap, and the end of the hold is
// one undo step. With `diagonal`, an arrow of the other axis joins and each axis keeps moving on its own.
std::string holdScript(PointDriver& d, bool diagonal) {
    auto hold = d.hold();
    if (!hold)
        return "no key hold: an arrow steps once per press";
    auto& keys = *hold->keys;
    keys.alt = true;
    hold->install();
    const int p = d.movePoint();
    d.selectPoint(p);
    const double x0 = d.position(p);
    const int steps = d.editSteps();
    auto frame = [&](double ms) {
        keys.now = 1000.0 + hold->tapMs + ms;
        hold->tick();
    };

    keys.down = {juce::KeyPress::rightKey};
    if (!d.moveKey(juce::KeyPress::rightKey) || !hold->active())
        return "the held arrow started no hold";
    frame(300.0);
    const double afterGlide = d.heldPosition(p);
    if (!(afterGlide > x0))
        return "the held arrow did not move the point";
    if (!diagonal) {
        frame(800.0);
        if (!(d.heldPosition(p) > afterGlide))
            return "the point stopped gliding while the arrow was down";
    } else {
        const double y0 = d.heldValue(p);
        keys.down = {juce::KeyPress::rightKey, juce::KeyPress::upKey};
        d.moveKey(juce::KeyPress::upKey);
        frame(600.0);
        frame(900.0);
        if (!(d.heldValue(p) > y0) || !(d.heldPosition(p) > afterGlide))
            return "the second arrow did not move the other axis along with the first";
        const double xBoth = d.heldPosition(p), yBoth = d.heldValue(p);
        keys.down = {juce::KeyPress::upKey};
        hold->keyUp();
        frame(1200.0);
        if (std::abs(d.heldPosition(p) - xBoth) > 1e-9 || !(d.heldValue(p) > yBoth))
            return "letting one arrow go did not leave the other carrying on alone";
    }
    keys.down.clear();
    hold->keyUp();
    if (hold->active())
        return "the hold did not end with the last key";
    return d.editSteps() == steps + 1 ? "" : said("the whole hold was ", d.editSteps() - steps, " undo steps");
}

std::string heldGlide(PointDriver& d) { return holdScript(d, false); }
std::string diagonal(PointDriver& d) { return holdScript(d, true); }

std::string duplicate(PointDriver& d) {
    d.selectPoint(1);
    const int n = d.points(), steps = d.editSteps();
    if (!d.duplicate())
        return "Cmd+D was not taken";
    if (d.points() != n + 1)
        return said("Cmd+D left ", d.points(), " points, expected ", n + 1);
    if (d.editSteps() != steps + 1)
        return said("a duplicate was ", d.editSteps() - steps, " undo steps");
    if (d.selected() != 1 || d.selectedIndex() == 1)
        return "the copy did not become the selection";
    if (d.ordered() && (d.selectedIndex() != 2 || !(d.position(2) > d.position(1)) || !(d.position(2) < d.position(3))))
        return "the copy did not land right after the original";
    return "";
}

std::string deleteNext(PointDriver& d) {
    d.selectPoint(1);
    const int n = d.points(), steps = d.editSteps();
    const double nextPos = d.position(2), nextVal = d.value(2);
    if (!d.key(juce::KeyPress::deleteKey, 0))
        return "Delete was not taken";
    if (d.points() != n - 1)
        return said("Delete left ", d.points(), " points, expected ", n - 1);
    if (d.editSteps() != steps + 1)
        return said("a delete was ", d.editSteps() - steps, " undo steps");
    if (d.selected() != 1 || d.selectedIndex() != 1 || d.position(1) != nextPos || d.value(1) != nextVal)
        return "the selection did not go to the next point";
    return "";
}

// Delete on a middle point whose slot number is out of axis order: the selection still goes to the next one along the
// axis, not to the next slot.
std::string deleteNextRenumbered(PointDriver& d) {
    if (!d.renumberPoints())
        return "the editor could not renumber its points";
    return deleteNext(d);
}

// The floating value readout shows for a hover and for a change, never for a selection alone.
std::string readoutVisibility(PointDriver& d) {
    d.selectPoint(1);
    d.leavePoint();
    if (d.floatingReadoutShowing())
        return "selecting a point showed its readout";
    d.hoverPoint(1);
    if (!d.floatingReadoutShowing())
        return "hovering a point showed no readout";
    d.leavePoint();
    if (d.floatingReadoutShowing())
        return "the readout stayed after the pointer left";
    if (!d.key(juce::KeyPress::upKey, kAlt) || !d.floatingReadoutShowing())
        return "a key nudge showed no readout";
    return "";
}

std::string doubleClickAdd(PointDriver& d) {
    const int n = d.points(), steps = d.editSteps();
    d.doubleClick(d.emptySpot());
    if (d.points() != n + 1)
        return said("a double-click on empty space left ", d.points(), " points, expected ", n + 1);
    return d.editSteps() == steps + 1 ? "" : said("an add was ", d.editSteps() - steps, " undo steps");
}

// A double-click on a middle dot removes it, with the same next-point selection rule as Delete.
std::string doubleClickDelete(PointDriver& d) {
    d.selectPoint(1);
    const int n = d.points(), steps = d.editSteps();
    const double nextPos = d.position(2), nextVal = d.value(2);
    d.doubleClick(d.dot(1));
    if (d.points() != n - 1)
        return said("a double-click on a dot left ", d.points(), " points, expected ", n - 1);
    if (d.editSteps() != steps + 1)
        return said("a delete was ", d.editSteps() - steps, " undo steps");
    if (d.selected() != 1 || d.selectedIndex() != 1 || d.position(1) != nextPos || d.value(1) != nextVal)
        return "the selection did not go to the next point";
    return "";
}

/** The level of a left-to-right line at `x`, linear between its samples. */
double lineAt(const std::vector<juce::Point<float>>& line, float x) {
    for (size_t i = 1; i < line.size(); ++i)
        if (line[i].x >= x && line[i].x > line[i - 1].x)
            return line[i - 1].y + (line[i].y - line[i - 1].y) * (x - line[i - 1].x) / (line[i].x - line[i - 1].x);
    return line.empty() ? 0.0 : line.back().y;
}

// One add or delete through `edit`, watched at `x`: the first frame still draws the old line, a little later the line
// is strictly between its two shapes, and it settles on the new one.
std::string lineEases(const LineHooks& hooks, float x, const char* what, const std::function<bool()>& edit) {
    const double before = lineAt(hooks.drawn(), x);
    if (!edit())
        return said(what, " was not taken");
    const double first = lineAt(hooks.drawn(), x);
    hooks.stepTo(40.0);
    const double mid = lineAt(hooks.drawn(), x);
    hooks.stepTo(1000.0);
    const double after = lineAt(hooks.drawn(), x);
    if (std::abs(after - before) < 2.0)
        return said(what, " did not change the line at the dot (", before, " -> ", after, ")");
    if (std::abs(first - before) > 1.5)
        return said(what, " jumped the line on its first frame (", before, " -> ", first, ")");
    const double fraction = (mid - before) / (after - before);
    if (!(fraction > 0.02 && fraction < 0.98))
        return said(what, ": 40 ms in, the line is ", fraction, " of the way to its new shape (not gliding)");
    return "";
}

// A delete and an add re-form the line: it glides from the old shape to the new one instead of popping or
// cross-fading.
std::string lineEasesOnAddDelete(PointDriver& d) {
    const auto hooks = d.line();
    if (!hooks)
        return "the editor gives no drawn line to check (add a LineHooks to its driver)";
    struct FullMotion {
        FullMotion() { synth::ui::setAnimationMode(synth::ui::AnimationMode::full); }
        ~FullMotion() { synth::ui::setAnimationMode(synth::ui::AnimationMode::followSystem); }
    } motion;
    d.selectPoint(1);
    if (auto problem = lineEases(*hooks, d.dot(1).x, "Delete", [&d] { return d.key(juce::KeyPress::deleteKey, 0); });
        !problem.empty())
        return problem;
    const juce::Point<float> spot{(d.dot(0).x + d.dot(1).x) * 0.5f, 2.0f};
    const int n = d.points();
    return lineEases(*hooks, spot.x, "a double-click on empty space", [&d, spot, n] {
        d.doubleClick(spot);
        return d.points() == n + 1;
    });
}

std::string rightClickMenu(PointDriver& d) {
    const auto area = d.rightClick(1);
    if (!area)
        return "a right-click on a point opened no menu";
    const auto at = d.surface().localPointToGlobal(d.dot(1));
    if (area->getCentre().toFloat().getDistanceFrom(at) > 12.0f)
        return said("the menu opens at ", area->getCentre().x, ",", area->getCentre().y, ", not at the point ", at.x,
                    ",", at.y);
    return "";
}

std::string edgeScroll(PointDriver& d) { return d.dragToEdge(); }

// Presses the middle of a segment and drags it `dy` pixels (y down); the curve at its half-way point must end up on
// that side of the straight line, in one undo step.
std::string bendOnce(PointDriver& d, bool rising, float dy, bool expectAbove) {
    const char* kind = rising ? "rising" : "falling";
    const auto seg = d.segment(rising);
    if (!seg)
        return said("no ", kind, " segment to bend");
    const int steps = d.editSteps();
    const auto to = juce::Point<float>(seg->mid.x, seg->mid.y + dy);
    d.press(seg->mid);
    d.dragTo(to);
    d.release(to);
    const double now = seg->midValue();
    if (expectAbove ? !(now > seg->chord + 1e-9) : !(now < seg->chord - 1e-9))
        return said("dragging a ", kind, " segment ", dy < 0.0f ? "up" : "down", " left its half-way point ",
                    now > seg->chord ? "above" : "not below", " the straight line (", now, " vs ", seg->chord, ")");
    if (d.editSteps() != steps + 1)
        return said("a bend drag was ", d.editSteps() - steps, " undo steps");
    return "";
}

// The curve follows the pointer: up bulges it up and down bulges it down, on a rising and on a falling segment.
std::string segmentDragFollows(PointDriver& d) {
    for (const bool rising : {true, false}) {
        if (auto problem = bendOnce(d, rising, -20.0f, true); !problem.empty())
            return problem;
        if (auto problem = bendOnce(d, rising, 40.0f, false); !problem.empty())
            return problem;
    }
    return "";
}

// Drags the selected point `pixels` past the dot after it (mods held during the drag only).
void dragPastNext(PointDriver& d, int p, float pixels, int mods) {
    d.selectPoint(p);
    const auto from = d.dot(p);
    const auto to = juce::Point<float>(d.dot(p + 1).x + pixels, from.y);
    d.press(from);
    d.dragTo(to, mods);
    d.release(to, mods);
}

// A single dot stops at its neighbour: a mouse drag well past it, and then Alt+Right pressed over and over, leave the
// neighbour where it was and the dot at or before it.
std::string stopAtNeighbour(PointDriver& d) {
    const int p = d.movePoint();
    if (p + 1 >= d.points())
        return "no point after the moved one";
    const double start = d.position(p), next = d.position(p + 1);
    dragPastNext(d, p, 40.0f, 0);
    if (!(d.position(p) > start))
        return "the drag did not move the dot toward its neighbour";
    if (d.position(p + 1) != next || d.position(p) > next + 1e-9)
        return said("a drag past the neighbour ended at ", d.position(p), " (order changed or passed; neighbour at ",
                    next, ")");
    for (int i = 0; i < 200; ++i)
        d.moveKey(juce::KeyPress::rightKey);
    if (d.position(p + 1) != next || d.position(p) > next + 1e-9)
        return said("Alt+Right pressed repeatedly ended at ", d.position(p), " (neighbour at ", next, ")");
    return "";
}

// The pass gesture restores passing: Option held in a drag takes the dot beyond its neighbour, and Alt+Shift+arrows
// take that dot back across it from the other side.
std::string passNeighbour(PointDriver& d) {
    const int p = d.movePoint();
    if (p + 1 >= d.points())
        return "no point after the moved one";
    const double next = d.position(p + 1);
    dragPastNext(d, p, 40.0f, kAlt);
    if (d.position(p) != next || !(d.position(p + 1) > next))
        return said("an Option-drag past the neighbour left the order unchanged (", d.position(p), ", ",
                    d.position(p + 1), ")");
    d.selectPoint(p + 1);
    for (int i = 0; i < 200 && d.position(p + 1) != next; ++i)
        d.key(juce::KeyPress::leftKey, kAlt | kShift);
    if (d.position(p + 1) != next)
        return "Alt+Shift+Left never passed the neighbour";
    return "";
}

} // namespace

const std::vector<Behaviour>& behaviours() {
    static const std::vector<Behaviour> all = {{"box-select", boxSelect},
                                               {"box-cursor-last-added", boxCursorLast},
                                               {"click-one-of-group", clickOneOfGroup},
                                               {"shift-extend", shiftExtend},
                                               {"alt-move", altMove},
                                               {"held-glide", heldGlide},
                                               {"two-arrows-diagonal", diagonal},
                                               {"duplicate", duplicate},
                                               {"delete-next", deleteNext},
                                               {"delete-next-renumbered", deleteNextRenumbered},
                                               {"readout-visibility", readoutVisibility},
                                               {"double-click-add", doubleClickAdd},
                                               {"double-click-delete", doubleClickDelete},
                                               {"line-eases-on-add-delete", lineEasesOnAddDelete},
                                               {"right-click-menu", rightClickMenu},
                                               {"edge-scroll", edgeScroll},
                                               {"segment-drag-follows", segmentDragFollows},
                                               {"stop-at-neighbour", stopAtNeighbour},
                                               {"pass-neighbour", passNeighbour}};
    return all;
}

} // namespace point_matrix
