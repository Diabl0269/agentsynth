// CardLayoutJson.cpp -- the version-2 JSON reader and writer for CardLayout (sections, items,
// conditions, hidden). The v1 slot format and the version dispatch live in CardLayout.cpp.
#include "CardLayoutJson.h"
#include <cmath>

namespace synth::detail {

namespace {

constexpr int kMinSpan = 1;
constexpr int kMaxSpan = 6;
constexpr int kMaxPosition = 4000;

template <typename Enum, std::size_t N>
struct NameTable {
    const char* names[N];

    const char* nameOf(Enum value) const { return names[static_cast<std::size_t>(value)]; }
    std::optional<Enum> find(const juce::String& name) const {
        for (std::size_t i = 0; i < N; ++i)
            if (name == names[i])
                return static_cast<Enum>(i);
        return std::nullopt;
    }
};

constexpr NameTable<CardWidget, 9> kWidgets{
    {"auto", "knob", "knobLarge", "faderV", "faderH", "toggle", "choice", "segmented", "stepper"}};
constexpr NameTable<CardView, 11> kViews{{"scope", "response", "spectrum", "envelope", "lfoShape", "lfoCurve",
                                          "waveform", "wavetable", "eqCurve", "threshold", "gainReduction"}};
constexpr NameTable<CardPresentation, 2> kPresentations{{"grid", "tab"}};
constexpr NameTable<CardConditionEffect, 2> kEffects{{"show", "dim"}};

bool isNumber(const juce::var& value) { return value.isInt() || value.isInt64() || value.isDouble(); }

// Reads an integer in [lo, hi]; an absent key yields `fallback`, a present one out of range is a failure.
bool readBoundedInt(const juce::DynamicObject& object, const char* key, int lo, int hi, int fallback, int& out) {
    if (!object.hasProperty(key)) {
        out = fallback;
        return true;
    }
    const auto& value = object.getProperty(key);
    if (!isNumber(value))
        return false;
    const double number = static_cast<double>(value);
    if (number < lo || number > hi || number != static_cast<double>(static_cast<int>(number)))
        return false;
    out = static_cast<int>(number);
    return true;
}

// A present number that is finite; a non-number or NaN/inf is a failure.
bool readFinite(const juce::DynamicObject& object, const char* key, double& out) {
    const auto& value = object.getProperty(key);
    if (!isNumber(value))
        return false;
    out = static_cast<double>(value);
    return std::isfinite(out);
}

bool readPosition(const juce::DynamicObject& object, std::optional<juce::Point<int>>& out) {
    const bool hasX = object.hasProperty("x");
    if (hasX != object.hasProperty("y"))
        return false;
    if (!hasX)
        return true;
    int x = 0;
    int y = 0;
    if (!readBoundedInt(object, "x", 0, kMaxPosition, 0, x) || !readBoundedInt(object, "y", 0, kMaxPosition, 0, y))
        return false;
    out = juce::Point<int>(x, y);
    return true;
}

bool readRange(const juce::DynamicObject& object, std::optional<juce::Range<double>>& out) {
    const bool hasMin = object.hasProperty("min");
    if (hasMin != object.hasProperty("max"))
        return false;
    if (!hasMin)
        return true;
    double lo = 0.0;
    double hi = 0.0;
    if (!readFinite(object, "min", lo) || !readFinite(object, "max", hi) || lo >= hi)
        return false;
    out = juce::Range<double>(lo, hi);
    return true;
}

std::optional<juce::String> readOptionalString(const juce::DynamicObject& object, const char* key) {
    const auto& value = object.getProperty(key);
    if (value.isString())
        return value.toString();
    return std::nullopt;
}

juce::var conditionToVar(const CardCondition& condition) {
    juce::Array<juce::var> values;
    for (const auto& value : condition.is)
        values.add(value);
    auto* object = new juce::DynamicObject();
    object->setProperty("param", condition.param);
    object->setProperty("is", values);
    object->setProperty("effect", kEffects.nameOf(condition.effect));
    return juce::var(object);
}

// A present `when` / `visibleWhen` that does not parse fails the layout: dropping it would show a
// control the layout meant to hide or dim.
bool readCondition(const juce::DynamicObject& object, const char* key, std::optional<CardCondition>& out) {
    if (!object.hasProperty(key) || object.getProperty(key).isVoid())
        return true;
    const auto* conditionObject = object.getProperty(key).getDynamicObject();
    if (conditionObject == nullptr)
        return false;

    CardCondition condition;
    condition.param = conditionObject->getProperty("param").toString();
    const auto* values = conditionObject->getProperty("is").getArray();
    const auto effect = kEffects.find(conditionObject->getProperty("effect").toString());
    if (condition.param.isEmpty() || values == nullptr || !effect)
        return false;
    for (const auto& value : *values) {
        if (!value.isString())
            return false;
        condition.is.add(value.toString());
    }
    condition.effect = *effect;
    out = std::move(condition);
    return true;
}

juce::var itemToVar(const CardItem& item) {
    auto* object = new juce::DynamicObject();
    if (const auto* view = std::get_if<CardViewItem>(&item)) {
        object->setProperty("view", kViews.nameOf(view->view));
        object->setProperty("open", view->open);
        return juce::var(object);
    }
    const auto& param = std::get<CardParamItem>(item);
    object->setProperty("paramId", param.paramId);
    if (param.indexHint >= 0)
        object->setProperty("indexHint", param.indexHint);
    if (param.node)
        object->setProperty("node", *param.node);
    object->setProperty("widget", kWidgets.nameOf(param.widget));
    if (param.label)
        object->setProperty("label", *param.label);
    object->setProperty("span", param.span);
    if (param.when)
        object->setProperty("when", conditionToVar(*param.when));
    if (param.at) {
        object->setProperty("x", param.at->x);
        object->setProperty("y", param.at->y);
    }
    if (param.range) {
        object->setProperty("min", param.range->getStart());
        object->setProperty("max", param.range->getEnd());
    }
    return juce::var(object);
}

bool readParamItem(const juce::DynamicObject& object, CardParamItem& out) {
    out.paramId = object.getProperty("paramId").toString();
    if (out.paramId.isEmpty())
        return false;
    int hint = -1;
    if (!readBoundedInt(object, "indexHint", -1, 1 << 20, -1, hint))
        return false;
    out.indexHint = hint;
    out.node = readOptionalString(object, "node");
    out.label = readOptionalString(object, "label");
    if (object.hasProperty("widget")) {
        const auto widget = kWidgets.find(object.getProperty("widget").toString());
        if (!widget)
            return false;
        out.widget = *widget;
    }
    return readBoundedInt(object, "span", kMinSpan, kMaxSpan, 1, out.span) && readCondition(object, "when", out.when) &&
           readPosition(object, out.at) && readRange(object, out.range);
}

bool readItem(const juce::var& json, CardItem& out) {
    const auto* object = json.getDynamicObject();
    if (object == nullptr)
        return false;
    if (object->hasProperty("view")) {
        const auto view = kViews.find(object->getProperty("view").toString());
        if (!view)
            return false;
        CardViewItem item;
        item.view = *view;
        if (object->hasProperty("open") && !object->getProperty("open").isBool())
            return false;
        item.open = !object->hasProperty("open") || static_cast<bool>(object->getProperty("open"));
        out = item;
        return true;
    }
    CardParamItem item;
    if (!readParamItem(*object, item))
        return false;
    out = std::move(item);
    return true;
}

juce::var sectionToVar(const CardSection& section) {
    juce::Array<juce::var> items;
    for (const auto& item : section.items)
        items.add(itemToVar(item));

    auto* object = new juce::DynamicObject();
    object->setProperty("id", section.id);
    if (section.title)
        object->setProperty("title", *section.title);
    object->setProperty("columns", section.columns);
    object->setProperty("presentation", kPresentations.nameOf(section.presentation));
    if (section.visibleWhen)
        object->setProperty("visibleWhen", conditionToVar(*section.visibleWhen));
    object->setProperty("items", items);
    return juce::var(object);
}

bool readSection(const juce::var& json, CardSection& out) {
    const auto* object = json.getDynamicObject();
    const auto* items = object != nullptr ? object->getProperty("items").getArray() : nullptr;
    if (items == nullptr)
        return false;

    out.id = object->getProperty("id").toString();
    out.title = readOptionalString(*object, "title");
    if (object->hasProperty("presentation")) {
        const auto presentation = kPresentations.find(object->getProperty("presentation").toString());
        if (!presentation)
            return false;
        out.presentation = *presentation;
    }
    if (!readBoundedInt(*object, "columns", 1, 6, CardSection::kDefaultColumns, out.columns) ||
        !readCondition(*object, "visibleWhen", out.visibleWhen))
        return false;

    for (const auto& itemJson : *items) {
        CardItem item;
        if (!readItem(itemJson, item))
            return false;
        out.items.push_back(std::move(item));
    }
    return true;
}

} // namespace

CardSlotKind slotKindFromWidget(CardWidget widget) {
    switch (widget) {
    case CardWidget::Auto:
        return CardSlotKind::Auto;
    case CardWidget::Toggle:
        return CardSlotKind::Toggle;
    case CardWidget::Choice:
    case CardWidget::Segmented:
    case CardWidget::Stepper:
        return CardSlotKind::Choice;
    case CardWidget::Knob:
    case CardWidget::KnobLarge:
    case CardWidget::FaderV:
    case CardWidget::FaderH:
        return CardSlotKind::Knob;
    }
    return CardSlotKind::Auto;
}

CardWidget widgetFromSlotKind(CardSlotKind kind) {
    switch (kind) {
    case CardSlotKind::Knob:
        return CardWidget::Knob;
    case CardSlotKind::Toggle:
        return CardWidget::Toggle;
    case CardSlotKind::Choice:
        return CardWidget::Choice;
    case CardSlotKind::Auto:
        break;
    }
    return CardWidget::Auto;
}

CardSection sectionFromSlots(const std::vector<CardSlot>& slots) {
    CardSection section;
    section.id = "main";
    for (const auto& slot : slots) {
        CardParamItem item;
        item.paramId = slot.paramId;
        item.indexHint = slot.indexHint;
        item.label = slot.label;
        item.widget = widgetFromSlotKind(slot.kind);
        section.items.emplace_back(std::move(item));
    }
    return section;
}

juce::var layoutToVarV2(const CardLayout& layout) {
    juce::Array<juce::var> sections;
    if (layout.sections.empty())
        sections.add(sectionToVar(sectionFromSlots(layout.slots)));
    for (const auto& section : layout.sections)
        sections.add(sectionToVar(section));

    juce::Array<juce::var> hidden;
    for (const auto& id : layout.hidden)
        hidden.add(id);

    auto* object = new juce::DynamicObject();
    object->setProperty("version", CardLayout::kCurrentVersion);
    if (layout.basedOn)
        object->setProperty("basedOn", *layout.basedOn);
    object->setProperty("sections", sections);
    object->setProperty("hidden", hidden);
    return juce::var(object);
}

bool layoutFromObjectV2(const juce::DynamicObject& object, CardLayout& out) {
    const auto* sections = object.getProperty("sections").getArray();
    if (sections == nullptr)
        return false;

    CardLayout layout;
    layout.version = CardLayout::kCurrentVersion;
    layout.basedOn = readOptionalString(object, "basedOn");
    for (const auto& sectionJson : *sections) {
        CardSection section;
        if (!readSection(sectionJson, section))
            return false;
        layout.sections.push_back(std::move(section));
    }
    if (object.hasProperty("hidden")) {
        const auto* hidden = object.getProperty("hidden").getArray();
        if (hidden == nullptr)
            return false;
        for (const auto& id : *hidden) {
            if (!id.isString() || id.toString().isEmpty())
                return false;
            layout.hidden.add(id.toString());
        }
    }
    out = std::move(layout);
    return true;
}

} // namespace synth::detail
