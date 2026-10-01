// CardLayout.cpp -- JSON round-trip and the Auto-kind derivation for the plugin-agnostic card
// layout value type (docs/control/plugin-card-layout.md#the-cardlayout-type-and-where-a-layout-comes-from).
#include "CardLayout.h"
#include "CardLayoutJson.h"
#include <algorithm>

namespace synth {

namespace {

constexpr const char* kKindNames[] = {"auto", "knob", "toggle", "choice"};

std::optional<CardSlotKind> kindFromString(const juce::String& name) {
    for (int i = 0; i < 4; ++i)
        if (name == kKindNames[i])
            return static_cast<CardSlotKind>(i);
    return std::nullopt;
}

// Reads one slot object; nullopt when it is not an object, has no paramId, or names an unknown kind.
// A layout with any such slot is refused whole rather than quietly losing a knob the user chose.
std::optional<CardSlot> slotFromVar(const juce::var& json) {
    auto* object = json.getDynamicObject();
    if (object == nullptr)
        return std::nullopt;

    CardSlot slot;
    slot.paramId = object->getProperty("paramId").toString();
    if (slot.paramId.isEmpty())
        return std::nullopt;

    const auto& hint = object->getProperty("indexHint");
    slot.indexHint = hint.isInt() || hint.isInt64() || hint.isDouble() ? static_cast<int>(hint) : -1;

    const auto& label = object->getProperty("label");
    if (label.isString())
        slot.label = label.toString();

    const auto& kind = object->getProperty("kind");
    if (kind.isString()) {
        const auto parsed = kindFromString(kind.toString());
        if (!parsed)
            return std::nullopt;
        slot.kind = *parsed;
    }
    return slot;
}

juce::var slotToVar(const CardSlot& slot) {
    auto* object = new juce::DynamicObject();
    object->setProperty("paramId", slot.paramId);
    object->setProperty("indexHint", slot.indexHint);
    object->setProperty("label", slot.label ? juce::var(*slot.label) : juce::var());
    object->setProperty("kind", kKindNames[static_cast<int>(slot.kind)]);
    return juce::var(object);
}

} // namespace

juce::var CardLayout::toVar() const {
    if (usesV2Features())
        return detail::layoutToVarV2(*this);

    juce::Array<juce::var> slotVars;
    for (const auto& slot : flatSlots())
        slotVars.add(slotToVar(slot));

    auto* object = new juce::DynamicObject();
    object->setProperty("version", kLegacyVersion);
    object->setProperty("slots", slotVars);
    return juce::var(object);
}

// A missing/non-integer version is Malformed; a version above kCurrentVersion is UnsupportedVersion
// so the caller can say "made by a newer Agent Synth" instead of "corrupt". Extra top-level
// properties are ignored on purpose: PluginCardLayoutStore's files carry the plugin's identity
// beside the layout, and a future minor addition must not read as corruption.
CardLayoutParseResult CardLayout::fromVar(const juce::var& json) {
    CardLayoutParseResult result;
    auto* object = json.getDynamicObject();
    if (object == nullptr)
        return result;

    const auto& versionVar = object->getProperty("version");
    if (!versionVar.isInt() && !versionVar.isInt64() && !versionVar.isDouble())
        return result;
    const int parsedVersion = static_cast<int>(versionVar);
    if (parsedVersion > kCurrentVersion) {
        result.status = ParseStatus::UnsupportedVersion;
        return result;
    }
    if (parsedVersion < kLegacyVersion)
        return result;

    CardLayout layout;
    if (parsedVersion >= kCurrentVersion) {
        if (!detail::layoutFromObjectV2(*object, layout))
            return result;
    } else {
        const auto* slotArray = object->getProperty("slots").getArray();
        if (slotArray == nullptr)
            return result;
        layout.version = parsedVersion;
        for (const auto& slotVar : *slotArray) {
            auto slot = slotFromVar(slotVar);
            if (!slot)
                return result;
            layout.slots.push_back(std::move(*slot));
        }
    }

    result.status = ParseStatus::Ok;
    result.layout = std::move(layout);
    return result;
}

// What a flat v1 slot list can hold: one untitled default-width grid section of plain params.
// Anything else (a title, a view, a span, a condition, a widget beyond the v1 kinds, hidden ids, a
// basedOn) needs v2.
bool CardLayout::usesV2Features() const {
    if (!hidden.isEmpty() || basedOn)
        return true;
    if (sections.empty())
        return false;
    if (sections.size() > 1)
        return true;

    const auto& section = sections.front();
    if (section.title || section.visibleWhen || section.presentation != CardPresentation::Grid ||
        section.columns != CardSection::kDefaultColumns)
        return true;
    for (const auto& item : section.items) {
        const auto* param = std::get_if<CardParamItem>(&item);
        if (param == nullptr || param->node || param->when || param->span != 1)
            return true;
        if (param->widget != CardWidget::Auto && param->widget != CardWidget::Knob &&
            param->widget != CardWidget::Toggle && param->widget != CardWidget::Choice)
            return true;
    }
    return false;
}

std::vector<CardSlot> CardLayout::flatSlots() const {
    if (sections.empty())
        return slots;

    std::vector<CardSlot> flat;
    for (const auto& section : sections) {
        for (const auto& item : section.items) {
            const auto* param = std::get_if<CardParamItem>(&item);
            if (param == nullptr)
                continue;
            CardSlot slot;
            slot.paramId = param->paramId;
            slot.indexHint = param->indexHint;
            slot.label = param->label;
            slot.kind = detail::slotKindFromWidget(param->widget);
            flat.push_back(std::move(slot));
        }
    }
    return flat;
}

CardLayout upgradeV1(const CardLayout& layout, const juce::StringArray& allParamIds) {
    if (!layout.sections.empty())
        return layout;

    CardLayout upgraded;
    upgraded.version = CardLayout::kCurrentVersion;
    upgraded.basedOn = layout.basedOn;
    upgraded.sections.push_back(detail::sectionFromSlots(layout.slots));
    upgraded.hidden = layout.hidden;
    for (const auto& id : allParamIds) {
        const bool placed = std::any_of(layout.slots.begin(), layout.slots.end(),
                                        [&id](const CardSlot& slot) { return slot.paramId == id; });
        if (!placed && !upgraded.hidden.contains(id))
            upgraded.hidden.add(id);
    }
    return upgraded;
}

CardSlotKind deriveSlotKind(const juce::AudioProcessorParameter& param) {
    if (param.isBoolean())
        return CardSlotKind::Toggle;
    if (param.isDiscrete() && param.getAllValueStrings().size() > 0)
        return CardSlotKind::Choice;
    return CardSlotKind::Knob;
}

CardSlotKind effectiveSlotKind(const CardSlot& slot, const juce::AudioProcessorParameter* param) {
    if (slot.kind != CardSlotKind::Auto)
        return slot.kind;
    return param != nullptr ? deriveSlotKind(*param) : CardSlotKind::Knob;
}

} // namespace synth
