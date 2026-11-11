// PointEditorMatrixProbes.cpp -- the behaviour columns of the point editor matrix (docs/control/point-editing.md): each
// probe is one short script of real key and mouse events against the PointDriver of an editor.
#include "PointEditorMatrixProbes.h"
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

std::string doubleClickAdd(PointDriver& d) {
    const int n = d.points(), steps = d.editSteps();
    d.doubleClick(d.emptySpot());
    if (d.points() != n + 1)
        return said("a double-click on empty space left ", d.points(), " points, expected ", n + 1);
    return d.editSteps() == steps + 1 ? "" : said("an add was ", d.editSteps() - steps, " undo steps");
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

} // namespace

const std::vector<Behaviour>& behaviours() {
    static const std::vector<Behaviour> all = {
        {"box-select", boxSelect},   {"shift-extend", shiftExtend},        {"alt-move", altMove},
        {"held-glide", heldGlide},   {"two-arrows-diagonal", diagonal},    {"duplicate", duplicate},
        {"delete-next", deleteNext}, {"double-click-add", doubleClickAdd}, {"right-click-menu", rightClickMenu},
        {"edge-scroll", edgeScroll}};
    return all;
}

} // namespace point_matrix
