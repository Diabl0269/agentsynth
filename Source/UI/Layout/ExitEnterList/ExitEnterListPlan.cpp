// ExitEnterListPlan.cpp: see ExitEnterListPlan.h.
#include "ExitEnterListPlan.h"

#include <algorithm>

namespace synth::ui {

namespace {
const ExitEnterListRow* find(const std::vector<ExitEnterListRow>& rows, const juce::String& key) {
    const auto it = std::find_if(rows.begin(), rows.end(), [&key](const ExitEnterListRow& r) { return r.key == key; });
    return it == rows.end() ? nullptr : &*it;
}

float endOf(const std::vector<ExitEnterListRow>& rows) {
    return rows.empty() ? 0.0f : rows.back().start + rows.back().extent;
}

// What the picture shows past the last item: slides with the stack's end.
void addTail(std::vector<ExitEnterListItem>& items, float srcEnd, float pictureExtent, float fromEnd, float toEnd) {
    if (pictureExtent <= srcEnd)
        return;
    ExitEnterListItem tail;
    tail.srcStart = srcEnd;
    tail.extent = pictureExtent - srcEnd;
    tail.fromStart = fromEnd;
    tail.toStart = toEnd;
    items.push_back(tail);
}
} // namespace

std::vector<ExitEnterListItem> ExitEnterListPlan::forRemoval(const std::vector<Row>& before,
                                                             const std::vector<Row>& after, float pictureExtent) {
    std::vector<Item> items;
    for (const auto& b : before) {
        Item item;
        item.key = b.key;
        item.srcStart = item.fromStart = b.start;
        item.extent = b.extent;
        if (const auto* a = find(after, b.key)) {
            item.toStart = a->start;
        } else {
            item.role = Item::Role::Exit;
            item.toStart = b.start;
        }
        items.push_back(item);
    }
    const float afterEnd = after.empty() ? (before.empty() ? 0.0f : before.front().start) : endOf(after);
    addTail(items, endOf(before), pictureExtent, endOf(before), afterEnd);
    return items;
}

std::vector<ExitEnterListItem> ExitEnterListPlan::forInsertion(const std::vector<Row>& after,
                                                               const std::vector<juce::String>& added,
                                                               float pictureExtent) {
    std::vector<Item> items;
    float addedAbove = 0.0f;
    for (const auto& a : after) {
        Item item;
        item.key = a.key;
        item.srcStart = item.toStart = a.start;
        item.extent = a.extent;
        if (std::find(added.begin(), added.end(), a.key) != added.end()) {
            item.role = Item::Role::Enter;
            item.fromStart = a.start;
            addedAbove += a.extent;
        } else {
            item.fromStart = a.start - addedAbove;
        }
        items.push_back(item);
    }
    addTail(items, endOf(after), pictureExtent, endOf(after) - addedAbove, endOf(after));
    return items;
}

ExitEnterTimeline ExitEnterListPlan::timelineFor(const std::vector<Item>& items) {
    ExitEnterTimeline timeline;
    for (const auto& item : items) {
        if (item.role == Item::Role::Exit)
            timeline.hasExit = true;
        else if (item.role == Item::Role::Enter)
            timeline.hasEnter = true;
        else if (!item.isTail() && item.fromStart != item.toStart)
            timeline.hasGap = true;
    }
    return timeline;
}

std::optional<ExitEnterListPlan::Drawn>
ExitEnterListPlan::drawnAt(const Item& item, const ExitEnterTimeline::Frame& frame, bool reducedMotion) {
    Drawn d;
    d.extent = item.extent;
    if (item.role == Item::Role::Stay) {
        d.start = item.fromStart + (item.toStart - item.fromStart) * frame.gap;
        return d;
    }
    d.start = item.srcStart;
    if (item.role == Item::Role::Exit) {
        d.scale = ExitEnterTimeline::ghostScale(frame.exit, true, reducedMotion);
        d.alpha = ExitEnterTimeline::ghostAlpha(frame.exit, true, reducedMotion);
    } else if (frame.grow >= 1.0f) {
        d.outlineAlpha = std::max(0.0f, 1.0f - frame.outline);
    } else {
        d.scale = ExitEnterTimeline::ghostScale(frame.grow, false, reducedMotion);
        d.alpha = ExitEnterTimeline::ghostAlpha(frame.grow, false, reducedMotion);
    }
    if (d.scale <= 0.0f || d.alpha <= 0.0f)
        return std::nullopt;
    return d;
}

} // namespace synth::ui
