// OnCardControlOptions.cpp -- reading a control's options off the card and the pure edits the panel's
// fields make to a layout.
#include "OnCardControlOptions.h"
#include "OnCardAddControlModel.h"
#include "UI/Graph/CardBody/CardBody.h"
#include "UI/Graph/CardLayoutEditor/CardLayoutEditorModel.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace synth::ui {

namespace {

CardParamItem* findItem(CardLayout& layout, const juce::String& paramId) {
    for (auto& section : layout.sections)
        for (auto& item : section.items)
            if (auto* param = std::get_if<CardParamItem>(&item); param != nullptr && param->paramId == paramId)
                return param;
    return nullptr;
}

const CardParamItem* findItem(const CardLayout& layout, const juce::String& paramId) {
    for (const auto& section : layout.sections)
        for (const auto& item : section.items)
            if (const auto* param = std::get_if<CardParamItem>(&item); param != nullptr && param->paramId == paramId)
                return param;
    return nullptr;
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
    return offersPair(widgetChoices, current, CardWidget::FaderV, CardWidget::FaderH);
}

std::optional<ControlOptions> readControlOptions(const synth::CardBody& body,
                                                 const std::vector<CardLayoutEditorParam>& params,
                                                 const CardLayout& layout, const juce::String& paramId) {
    const auto& plan = body.getPlan();
    const int index = plan.findParam(paramId);
    const CardParamItem unlisted;
    const auto* listed = findItem(layout, paramId);
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

CardLayout withPlacedItem(CardLayout layout, const juce::String& paramId, const synth::CardBody& body) {
    const auto& plan = body.getPlan();
    const int index = plan.findParam(paramId);
    const int section = index >= 0 ? plan.items[(size_t)index].section : -1;
    if (section < 0 || findItem(layout, paramId) != nullptr)
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

CardLayout withShowAs(CardLayout layout, const juce::String& paramId, CardWidget widget) {
    if (auto* item = findItem(layout, paramId))
        item->widget = widget;
    return layout;
}

CardLayout withControlLabel(CardLayout layout, const juce::String& paramId, const juce::String& displayName,
                            const juce::String& text) {
    if (auto* item = findItem(layout, paramId))
        item->label = labelOverrideFor(text, displayName);
    return layout;
}

CardLayout withControlRange(CardLayout layout, const juce::String& paramId, std::optional<juce::Range<double>> range) {
    if (auto* item = findItem(layout, paramId))
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
