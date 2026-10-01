#include "ModMatrixKeyboard.h"

#include <algorithm>
#include <vector>

namespace synth::ui {

namespace {
using Line = std::vector<juce::Component*>;

bool isUsable(const juce::Component* c) {
    return c != nullptr && c->isVisible() && c->isEnabled() && c->getWantsKeyboardFocus();
}

// Every routing row is the parent of one source combo; its line lists its controls in column order.
void collectRowLines(juce::Component& node, std::vector<Line>& lines) {
    for (auto* child : node.getChildren()) {
        if (child->getComponentID() == modmatrix_ids::kSource) {
            auto& row = node;
            Line line;
            for (const auto* id : {modmatrix_ids::kSource, modmatrix_ids::kDest, modmatrix_ids::kAmount,
                                   modmatrix_ids::kBypass, modmatrix_ids::kDelete})
                if (auto* c = row.findChildWithID(id); isUsable(c))
                    line.push_back(c);
            if (!line.empty())
                lines.push_back(std::move(line));
            return;
        }
        collectRowLines(*child, lines);
    }
}

std::vector<Line> buildGrid(juce::Component& matrix) {
    std::vector<Line> grid;
    Line header;
    for (const auto* id : {modmatrix_ids::kAdd, modmatrix_ids::kFlat})
        if (auto* c = matrix.findChildWithID(id); isUsable(c))
            header.push_back(c);
    if (!header.empty())
        grid.push_back(std::move(header));

    std::vector<Line> rows;
    collectRowLines(matrix, rows);
    // On-screen order, whatever order the rows were added in.
    std::stable_sort(rows.begin(), rows.end(), [&matrix](const Line& a, const Line& b) {
        return matrix.getLocalPoint(a.front(), juce::Point<int>()).y <
               matrix.getLocalPoint(b.front(), juce::Point<int>()).y;
    });
    grid.insert(grid.end(), rows.begin(), rows.end());
    return grid;
}

// The grid cell holding `from`, or the cell of the control `from` sits inside (a combo's label).
bool locate(const std::vector<Line>& grid, juce::Component* from, size_t& line, size_t& col) {
    for (size_t l = 0; l < grid.size(); ++l)
        for (size_t c = 0; c < grid[l].size(); ++c)
            if (grid[l][c] == from || grid[l][c]->isParentOf(from)) {
                line = l;
                col = c;
                return true;
            }
    return false;
}

// A row below the fold scrolls up to meet the focus, so the ring is never off screen.
void scrollIntoView(juce::Component& target) {
    auto* viewport = target.findParentComponentOfClass<juce::Viewport>();
    if (viewport == nullptr || viewport->getViewedComponent() == nullptr)
        return;
    const auto area = viewport->getViewedComponent()->getLocalArea(&target, target.getLocalBounds());
    const auto view = viewport->getViewArea();
    if (area.getY() < view.getY())
        viewport->setViewPosition(view.getX(), area.getY());
    else if (area.getBottom() > view.getBottom())
        viewport->setViewPosition(view.getX(), area.getBottom() - view.getHeight());
}

class ArrowKeys : public juce::KeyListener {
public:
    bool keyPressed(const juce::KeyPress& key, juce::Component* origin) override {
        if (origin == nullptr)
            return false;
        auto* matrix = origin->getComponentID() == "modMatrix" ? origin : nullptr;
        for (auto* p = origin; matrix == nullptr && p != nullptr; p = p->getParentComponent())
            if (p->getComponentID() == "modMatrix")
                matrix = p;
        if (matrix == nullptr)
            return false;
        auto* from = juce::Component::getCurrentlyFocusedComponent();
        auto* target = modMatrixArrowTarget(*matrix, from != nullptr ? from : matrix, key);
        if (target == nullptr)
            return false;
        if (target != from) {
            target->grabKeyboardFocus();
            scrollIntoView(*target);
        }
        return true;
    }
};
} // namespace

juce::Component* modMatrixArrowTarget(juce::Component& matrix, juce::Component* from, const juce::KeyPress& key) {
    const bool left = key == juce::KeyPress::leftKey, right = key == juce::KeyPress::rightKey;
    const bool up = key == juce::KeyPress::upKey, down = key == juce::KeyPress::downKey;
    if (!(left || right || up || down) || from == nullptr)
        return nullptr;
    if (from != &matrix && !matrix.isParentOf(from))
        return nullptr;

    const auto grid = buildGrid(matrix);
    if (grid.empty())
        return from;

    size_t line = 0, col = 0;
    if (!locate(grid, from, line, col))
        return grid.front().front(); // from the matrix itself: enter at the first control

    if (left || right) {
        if (right) {
            if (col + 1 < grid[line].size())
                return grid[line][col + 1];
            return line + 1 < grid.size() ? grid[line + 1].front() : from;
        }
        if (col > 0)
            return grid[line][col - 1];
        return line > 0 ? grid[line - 1].back() : from;
    }

    if (up && line == 0)
        return from;
    if (down && line + 1 >= grid.size())
        return from;
    const auto& next = grid[up ? line - 1 : line + 1];
    return next[std::min(col, next.size() - 1)];
}

juce::KeyListener& modMatrixArrowKeys() {
    static ArrowKeys listener;
    return listener;
}

} // namespace synth::ui
