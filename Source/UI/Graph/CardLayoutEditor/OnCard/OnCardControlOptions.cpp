// OnCardControlOptions.cpp -- reading a control's options off the card and the pure edits the panel's
// fields make to a layout.
#include "OnCardControlOptions.h"
#include "OnCardAddControlModel.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/CardLayoutEditor/CardLayoutEditorModel.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <type_traits>

namespace synth::ui {

namespace {

// `layoutSection` given: only that section's item (the ADSR's Separate looks each list Sustain); an index the
// layout does not have, or nullopt, takes the first item with the id anywhere.
template <typename Layout>
auto* findItemIn(Layout& layout, const juce::String& paramId, std::optional<int> layoutSection) {
    using Param = std::conditional_t<std::is_const_v<Layout>, const CardParamItem, CardParamItem>;
    const auto search = [&](auto& section) -> Param* {
        for (auto& item : section.items)
            if (auto* param = std::get_if<CardParamItem>(&item); param != nullptr && param->paramId == paramId)
                return param;
        return nullptr;
    };
    if (layoutSection.has_value() && *layoutSection >= 0 && *layoutSection < (int)layout.sections.size())
        return search(layout.sections[(size_t)*layoutSection]);
    for (auto& section : layout.sections)
        if (auto* found = search(section))
            return found;
    return static_cast<Param*>(nullptr);
}

CardParamItem* findItem(CardLayout& layout, const juce::String& paramId, std::optional<int> layoutSection) {
    return findItemIn(layout, paramId, layoutSection);
}

const CardParamItem* findItem(const CardLayout& layout, const juce::String& paramId, std::optional<int> layoutSection) {
    return findItemIn(layout, paramId, layoutSection);
}

// A whole-string number: getDoubleValue would read any text as 0.
std::optional<double> parseNumber(const juce::String& text) {
    const auto trimmed = text.trim();
    if (trimmed.isEmpty())
        return std::nullopt;
    const auto utf8 = trimmed.toStdString();
    char* end = nullptr;
    const double value = std::strtod(utf8.c_str(), &end);
    if (end != utf8.c_str() + utf8.size() || !std::isfinite(value))
        return std::nullopt;
    return value;
}

} // namespace

WidgetKind widgetKindOf(CardWidget widget) {
    switch (widget) {
    case CardWidget::Auto:
        return WidgetKind::Auto;
    case CardWidget::Knob:
    case CardWidget::KnobLarge:
        return WidgetKind::Knob;
    case CardWidget::FaderV:
    case CardWidget::FaderVLong:
    case CardWidget::FaderH:
        return WidgetKind::Fader;
    case CardWidget::Toggle:
        return WidgetKind::Toggle;
    case CardWidget::Choice:
        return WidgetKind::Menu;
    case CardWidget::Segmented:
        return WidgetKind::Segmented;
    case CardWidget::Stepper:
        return WidgetKind::Stepper;
    }
    return WidgetKind::Auto;
}

juce::String widgetKindName(WidgetKind kind) {
    switch (kind) {
    case WidgetKind::Auto:
        return "Automatic";
    case WidgetKind::Knob:
        return "Knob";
    case WidgetKind::Fader:
        return "Fader";
    case WidgetKind::Toggle:
        return "Toggle";
    case WidgetKind::Menu:
        return "Menu";
    case WidgetKind::Segmented:
        return "Segmented";
    case WidgetKind::Stepper:
        return "Stepper";
    }
    return "Automatic";
}

std::vector<WidgetKind> widgetKinds(const std::vector<CardWidget>& widgetChoices) {
    std::vector<WidgetKind> kinds;
    for (auto widget : widgetChoices) {
        const auto kind = widgetKindOf(widget);
        if (std::find(kinds.begin(), kinds.end(), kind) == kinds.end())
            kinds.push_back(kind);
    }
    return kinds;
}

CardWidget widgetForKind(WidgetKind kind, CardWidget current, const std::vector<CardWidget>& widgetChoices) {
    if (widgetKindOf(current) == kind)
        return current;
    for (auto widget : widgetChoices)
        if (widgetKindOf(widget) == kind)
            return widget;
    return current;
}

namespace {

bool offersPair(const std::vector<CardWidget>& choices, CardWidget current, CardWidget a, CardWidget b) {
    const auto has = [&](CardWidget w) { return std::find(choices.begin(), choices.end(), w) != choices.end(); };
    return (current == a || current == b) && has(a) && has(b);
}

} // namespace

bool offersKnobSize(const std::vector<CardWidget>& widgetChoices, CardWidget current) {
    return offersPair(widgetChoices, current, CardWidget::Knob, CardWidget::KnobLarge);
}

bool offersFaderDirection(const std::vector<CardWidget>& widgetChoices, CardWidget current) {
    return current == CardWidget::FaderVLong
               ? offersPair(widgetChoices, CardWidget::FaderV, CardWidget::FaderV, CardWidget::FaderH)
               : offersPair(widgetChoices, current, CardWidget::FaderV, CardWidget::FaderH);
}

bool offersFaderSize(const std::vector<CardWidget>& widgetChoices, CardWidget current) {
    return offersPair(widgetChoices, current, CardWidget::FaderV, CardWidget::FaderVLong);
}

std::optional<int> visibleLayoutSection(const synth::CardBody& body, const CardLayout& layout,
                                        const juce::String& paramId) {
    const auto& plan = body.getPlan();
    const int index = plan.findParam(paramId);
    if (index < 0 || plan.items[(size_t)index].alsoIn.empty())
        return std::nullopt;
    const auto& planned = plan.items[(size_t)index];
    std::vector<int> candidates{planned.section};
    for (const auto& placement : planned.alsoIn)
        candidates.push_back(placement.section);
    int chosen = planned.section;
    for (const int candidate : candidates)
        if (candidate >= 0 && candidate < (int)plan.sections.size() && plan.sections[(size_t)candidate].visible) {
            chosen = candidate;
            break;
        }
    if (chosen < 0 || chosen >= (int)plan.sections.size())
        return std::nullopt;
    int target = layoutSectionIndexOfPlan(layout, chosen);
    if (plan.sections[(size_t)chosen].footer)
        for (int i = 0; i < (int)layout.sections.size(); ++i)
            if (layout.sections[(size_t)i].id == CardSection::kFooterId)
                target = i;
    if (target < 0)
        return std::nullopt;
    return target;
}

std::optional<ControlOptions> readControlOptions(const synth::CardBody& body,
                                                 const std::vector<CardLayoutEditorParam>& params,
                                                 const CardLayout& layout, const juce::String& paramId,
                                                 std::optional<int> layoutSection) {
    const auto& plan = body.getPlan();
    const int index = plan.findParam(paramId);
    const CardParamItem unlisted;
    const auto* listed = findItem(layout, paramId, layoutSection);
    const auto* item = listed != nullptr ? listed : &unlisted;
    const auto found = std::find_if(params.begin(), params.end(),
                                    [&](const CardLayoutEditorParam& p) { return p.paramId == paramId; });
    if (index < 0 || found == params.end())
        return std::nullopt;
    const auto& planned = plan.items[(size_t)index];
    ControlOptions options;
    options.paramId = paramId;
    options.displayName = found->displayName;
    options.caption = item->label.value_or(found->displayName);
    options.widgetChoices = found->widgetChoices;
    options.widget = item->widget;
    if (options.widget == CardWidget::Auto && !options.widgetChoices.empty())
        options.widget = options.widgetChoices.front();
    options.range = item->range;
    if (planned.param != nullptr && dynamic_cast<const juce::AudioParameterFloat*>(planned.param) != nullptr &&
        isContinuousKind(planned.kind)) {
        const auto& full = planned.param->getNormalisableRange();
        options.fullRange = juce::Range<double>((double)full.start, (double)full.end);
    }
    return options;
}

CardLayout withPlacedItem(CardLayout layout, const juce::String& paramId, const synth::CardBody& body,
                          std::optional<int> layoutSection) {
    const auto& plan = body.getPlan();
    const int index = plan.findParam(paramId);
    const int section = index >= 0 ? plan.items[(size_t)index].section : -1;
    if (section < 0 || findItem(layout, paramId, layoutSection) != nullptr)
        return layout;
    int target = layoutSectionIndexOfPlan(layout, section);
    if (plan.sections[(size_t)section].footer)
        for (int i = 0; i < (int)layout.sections.size(); ++i)
            if (layout.sections[(size_t)i].id == CardSection::kFooterId)
                target = i;
    if (target < 0)
        return layout;
    CardParamItem item;
    item.paramId = paramId;
    layout.sections[(size_t)target].items.emplace_back(item);
    return layout;
}

CardLayout withShowAs(CardLayout layout, const juce::String& paramId, CardWidget widget,
                      std::optional<int> layoutSection) {
    if (auto* item = findItem(layout, paramId, layoutSection))
        item->widget = widget;
    return layout;
}

CardLayout withControlLabel(CardLayout layout, const juce::String& paramId, const juce::String& displayName,
                            const juce::String& text, std::optional<int> layoutSection) {
    if (auto* item = findItem(layout, paramId, layoutSection))
        item->label = labelOverrideFor(text, displayName);
    return layout;
}

CardLayout withControlRange(CardLayout layout, const juce::String& paramId, std::optional<juce::Range<double>> range,
                            std::optional<int> layoutSection) {
    if (auto* item = findItem(layout, paramId, layoutSection))
        item->range = range;
    return layout;
}

RangeEntry parseRangeEntry(const juce::String& minText, const juce::String& maxText, juce::Range<double> full) {
    RangeEntry entry;
    if (minText.trim().isEmpty() && maxText.trim().isEmpty()) {
        entry.ok = true;
        return entry;
    }
    const auto lo = minText.trim().isEmpty() ? std::optional<double>(full.getStart()) : parseNumber(minText);
    const auto hi = maxText.trim().isEmpty() ? std::optional<double>(full.getEnd()) : parseNumber(maxText);
    if (!lo.has_value() || !hi.has_value()) {
        entry.hint = "Enter numbers only.";
        return entry;
    }
    const double start = juce::jlimit(full.getStart(), full.getEnd(), *lo);
    const double end = juce::jlimit(full.getStart(), full.getEnd(), *hi);
    if (start >= end) {
        entry.hint = "Minimum must be below maximum, within " + formatRangeValue(full.getStart()) + " to " +
                     formatRangeValue(full.getEnd()) + ".";
        return entry;
    }
    entry.ok = true;
    if (start > full.getStart() || end < full.getEnd())
        entry.range = juce::Range<double>(start, end);
    return entry;
}

juce::String formatRangeValue(double value) {
    auto text = juce::String(value, 6);
    if (text.containsChar('.'))
        text = text.trimCharactersAtEnd("0").trimCharactersAtEnd(".");
    return text;
}

} // namespace synth::ui
