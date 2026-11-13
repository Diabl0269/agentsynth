#include "ControlStyleOverride.h"

namespace synth::theme {

juce::var toVar(const ControlStyleOverride& style) {
    if (style.isEmpty())
        return {};
    auto* object = new juce::DynamicObject();
    if (style.knob)
        object->setProperty("knob", juce::String(knobStyleId(*style.knob)));
    if (style.fader)
        object->setProperty("fader", juce::String(knobStyleId(*style.fader)));
    return juce::var(object);
}

ControlStyleOverride controlStyleOverrideFromVar(const juce::var& value) {
    ControlStyleOverride out;
    const auto* object = value.getDynamicObject();
    if (object == nullptr)
        return out;
    if (const auto& knob = object->getProperty("knob"); knob.isString())
        out.knob = knobStyleFromKnownId(knob.toString());
    if (const auto& fader = object->getProperty("fader"); fader.isString())
        out.fader = knobStyleFromKnownId(fader.toString());
    return out;
}

KnobStyle resolveControlStyle(std::optional<KnobStyle> instance, std::optional<KnobStyle> type,
                              KnobStyle app) noexcept {
    return instance ? *instance : (type ? *type : app);
}

namespace {
juce::var readTypeMap(juce::PropertiesFile& props) {
    const auto parsed = juce::JSON::parse(props.getValue(controlStyleByTypeKey()));
    return parsed.getDynamicObject() != nullptr ? parsed : juce::var();
}
} // namespace

ControlStyleOverride loadTypeControlStyle(juce::PropertiesFile& props, const juce::String& type) {
    const auto map = readTypeMap(props);
    if (const auto* object = map.getDynamicObject())
        return controlStyleOverrideFromVar(object->getProperty(type));
    return {};
}

void writeTypeControlStyle(juce::PropertiesFile& props, const juce::String& type, const ControlStyleOverride& style) {
    auto map = readTypeMap(props);
    auto* object = map.getDynamicObject();
    if (object == nullptr) {
        map = juce::var(new juce::DynamicObject());
        object = map.getDynamicObject();
    }
    if (style.isEmpty())
        object->removeProperty(type);
    else
        object->setProperty(type, toVar(style));
    if (object->getProperties().size() == 0)
        props.removeValue(controlStyleByTypeKey());
    else
        props.setValue(controlStyleByTypeKey(), juce::JSON::toString(map, true));
}

} // namespace synth::theme
