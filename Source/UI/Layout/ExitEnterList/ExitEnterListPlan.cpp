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

bool ExitEnterListPlan::forInsertionFromSource(const std::vector<Row>& after, const std::vector<juce::String>& added,
                                               const SourceOf& sourceOf, float pictureExtent,
                                               std::vector<Item>& items) {
    auto planned = forInsertion(after, added, pictureExtent);
    for (auto& item : planned) {
        if (item.role != Item::Role::Enter)
            continue;
        const auto source = sourceOf.find(item.key);
        if (source == sourceOf.end())
            return false;
        const auto it =
            std::find_if(planned.begin(), planned.end(), [&](const Item& o) { return o.key == source->second; });
        if (it == planned.end() || it->role != Item::Role::Stay)
            return false;
        item.travels = true;
        item.originStart = it->fromStart; // where the original stands as the motion begins
    }
    items = std::move(planned);
    return true;
}

bool ExitEnterListPlan::forRemovalIntoSource(const std::vector<Row>& before, const std::vector<Row>& after,
                                             const SourceOf& sourceOf, float pictureExtent, std::vector<Item>& items) {
    auto planned = forRemoval(before, after, pictureExtent);
    for (auto& item : planned) {
        if (item.role != Item::Role::Exit)
            continue;
        const auto source = sourceOf.find(item.key);
        if (source == sourceOf.end())
            return false;
        const auto it =
            std::find_if(planned.begin(), planned.end(), [&](const Item& o) { return o.key == source->second; });
        if (it == planned.end() || it->role != Item::Role::Stay)
            return false;
        item.travels = true;
        item.originStart = it->toStart; // where the original ends up once the rows have closed
    }
    items = std::move(planned);
    return true;
}

ExitEnterTimeline ExitEnterListPlan::timelineFor(const std::vector<Item>& items) {
    ExitEnterTimeline timeline;
    for (const auto& item : items)
        if (item.travels) {
            timeline.together = true;
            timeline.reverse = item.role == Item::Role::Exit;
            timeline.togetherMs =
                timeline.reverse ? ExitEnterTimeline::kIntoSourceMs : ExitEnterTimeline::kOutOfSourceMs;
            timeline.hasEnter = item.role == Item::Role::Enter;
            timeline.hasExit = timeline.reverse;
            timeline.outline = false;
            return timeline;
        }
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

namespace {
constexpr float kLeavingScale = 0.8f; // a copy is this big on its original, full size in its slot
constexpr float kLeavingAlpha = 0.75f;

// A slice that travels between its original and its slot. `out` is how far it is from the original, 0..1.
ExitEnterListPlan::Drawn travelling(const ExitEnterListItem& item, float out, bool reducedMotion) {
    ExitEnterListPlan::Drawn d;
    d.extent = item.extent;
    if (reducedMotion) { // nothing moves: the slice sits in its slot and fades
        d.start = item.srcStart;
        d.alpha = out;
        return d;
    }
    d.start = item.originStart + (item.srcStart - item.originStart) * out;
    d.scale = kLeavingScale + (1.0f - kLeavingScale) * out;
    d.alpha = kLeavingAlpha + (1.0f - kLeavingAlpha) * out;
    d.lift = out < 1.0f ? 1.0f - out : 0.0f;
    return d;
}
} // namespace

std::optional<ExitEnterListPlan::Drawn>
ExitEnterListPlan::drawnAt(const Item& item, const ExitEnterTimeline::Frame& frame, bool reducedMotion) {
    if (item.travels) {
        const float out = item.role == Item::Role::Enter ? frame.grow : 1.0f - frame.exit;
        if (out <= 0.0f && reducedMotion)
            return std::nullopt;
        return travelling(item, std::clamp(out, 0.0f, 1.0f), reducedMotion);
    }
    Drawn d;
    d.extent = item.extent;
    if (item.role == Item::Role::Stay) {
        // Together with a fading copy under Reduce Motion, the rows are in place at once.
        const float gap = reducedMotion && frame.together ? 1.0f : frame.gap;
        d.start = item.fromStart + (item.toStart - item.fromStart) * gap;
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
