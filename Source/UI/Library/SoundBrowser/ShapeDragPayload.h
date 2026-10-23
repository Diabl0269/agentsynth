#pragma once

#include "Modules/Shapes/ShapeLibrary.h"
#include <juce_core/juce_core.h>
#include <optional>

// What a Shape row carries while it is dragged: "shape:<kind>:<factory|user>:<name>". The prefix keeps it apart from
// the other drag payloads (module names, snippets, plugin identities, sounds), none of which start with it. The target
// looks the curve up by name when the drop lands, so the payload never holds a curve.
// Rules for the targets: docs/layout/sound-browser.md#dragging-a-shape
namespace synth::ui {

constexpr const char* kShapePayloadPrefix = "shape:";

/** Which shape a payload names. */
struct ShapeRef {
    synth::shapes::ShapeKind kind = synth::shapes::ShapeKind::Filter;
    bool factory = false;
    juce::String name;
};

inline juce::String makeShapePayload(const ShapeRef& ref) {
    return juce::String(kShapePayloadPrefix) + synth::shapes::kindWord(ref.kind) +
           (ref.factory ? ":factory:" : ":user:") + ref.name;
}

/** True for a payload made by makeShapePayload. */
inline bool isShapePayload(const juce::var& description) {
    return description.isString() && description.toString().startsWith(kShapePayloadPrefix);
}

/** The shape a payload names; none for any other payload or a malformed one. */
inline std::optional<ShapeRef> shapeFromPayload(const juce::var& description) {
    if (!isShapePayload(description))
        return std::nullopt;
    const auto body = description.toString().substring((int)juce::String(kShapePayloadPrefix).length());
    const auto kindEnd = body.indexOfChar(':');
    const auto originEnd = kindEnd < 0 ? -1 : body.indexOfChar(kindEnd + 1, ':');
    if (kindEnd < 0 || originEnd < 0)
        return std::nullopt;
    const auto kind = synth::shapes::kindFromWord(body.substring(0, kindEnd));
    const auto origin = body.substring(kindEnd + 1, originEnd);
    const auto name = body.substring(originEnd + 1);
    if (!kind.has_value() || (origin != "factory" && origin != "user") || name.isEmpty())
        return std::nullopt;
    return ShapeRef{*kind, origin == "factory", name};
}

} // namespace synth::ui
