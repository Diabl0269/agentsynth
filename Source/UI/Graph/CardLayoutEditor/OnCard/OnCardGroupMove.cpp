// OnCardGroupMove.cpp -- the order and titles of a card's groups as pure layout changes.
#include "OnCardGroupMove.h"
#include <algorithm>
#include <numeric>

namespace synth::ui::oncard {

std::vector<int> stackedTops(const std::vector<GroupBlock>& blocks, const std::vector<int>& order) {
    std::vector<int> tops(blocks.size(), 0);
    if (blocks.empty())
        return tops;
    int y = blocks.front().top;
    for (int index : order) {
        if (index < 0 || index >= (int)blocks.size())
            continue;
        tops[(size_t)index] = y;
        y += blocks[(size_t)index].extent;
    }
    return tops;
}

std::vector<int> orderAfterMoving(int count, int from, int to) {
    std::vector<int> order((size_t)std::max(0, count));
    std::iota(order.begin(), order.end(), 0);
    if (from < 0 || from >= count || to < 0 || to >= count)
        return order;
    order.erase(order.begin() + from);
    order.insert(order.begin() + to, from);
    return order;
}

bool isIdentityOrder(const std::vector<int>& order) {
    for (size_t i = 0; i < order.size(); ++i)
        if (order[i] != (int)i)
            return false;
    return true;
}

juce::String describeGroupMove(const std::vector<juce::String>& titles, const std::vector<int>& order, int moved) {
    const auto place = std::find(order.begin(), order.end(), moved);
    if (place == order.end() || moved < 0 || moved >= (int)titles.size() || moved == (int)(place - order.begin()))
        return {};
    const int now = (int)(place - order.begin());
    // Moving down passes the group that is now just above; moving up, the one now just below.
    const bool down = now > moved;
    const int neighbour = order[(size_t)(down ? now - 1 : now + 1)];
    return titles[(size_t)moved] + " moved " + (down ? "below " : "above ") + titles[(size_t)neighbour];
}

CardLayout withGroupsReordered(CardLayout layout, const std::vector<int>& slots, const std::vector<int>& order) {
    const int count = (int)layout.sections.size();
    const auto valid = [count](int index) { return index >= 0 && index < count; };
    if (slots.size() != order.size() || !std::all_of(slots.begin(), slots.end(), valid))
        return layout;
    for (int k : order)
        if (k < 0 || k >= (int)slots.size())
            return layout;
    const auto before = layout.sections;
    for (size_t k = 0; k < slots.size(); ++k)
        layout.sections[(size_t)slots[k]] = before[(size_t)slots[(size_t)order[k]]];
    return layout;
}

CardLayout withGroupTitle(CardLayout layout, int index, const juce::String& title) {
    const auto trimmed = title.trim();
    if (trimmed.isEmpty() || index < 0 || index >= (int)layout.sections.size())
        return layout;
    layout.sections[(size_t)index].title = trimmed;
    return layout;
}

} // namespace synth::ui::oncard
