// CardLayout.cpp -- JSON round-trip and the Auto-kind derivation for the plugin-agnostic card
// layout value type (docs/control/plugin-card-layout.md#the-cardlayout-type-and-where-a-layout-comes-from).
#include "CardLayout.h"

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
    juce::Array<juce::var> slotVars;
    for (const auto& slot : slots)
        slotVars.add(slotToVar(slot));

    auto* object = new juce::DynamicObject();
    object->setProperty("version", version);
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
    if (parsedVersion < 1)
        return result;

    const auto* slotArray = object->getProperty("slots").getArray();
    if (slotArray == nullptr)
        return result;

    CardLayout layout;
    layout.version = parsedVersion;
    for (const auto& slotVar : *slotArray) {
        auto slot = slotFromVar(slotVar);
        if (!slot)
            return result;
        layout.slots.push_back(std::move(*slot));
    }

    result.status = ParseStatus::Ok;
    result.layout = std::move(layout);
    return result;
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
