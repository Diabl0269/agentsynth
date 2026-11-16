// Concern: the OSC address rules and the osc-only JSON fields of MessageSpec / Control.
#include "RemoteModelJsonOsc.h"

#include <cmath>

namespace synth {

namespace {

bool isPatternChar(juce::juce_wchar c) noexcept {
    return c == '*' || c == '?' || c == '[' || c == ']' || c == '{' || c == '}';
}

bool readFinite(const juce::var& v, double& out) {
    if (!(v.isDouble() || v.isInt() || v.isInt64()))
        return false;
    out = static_cast<double>(v);
    return std::isfinite(out);
}

} // namespace

// Iterates the string's own UTF-8 bytes: no String is constructed, so this is safe on the network thread's
// hot path and in the engine's lookup.
bool isValidOscAddress(const juce::String& address) noexcept {
    if (address.getNumBytesAsUTF8() > static_cast<size_t>(kMaxOscAddressBytes) || address.isEmpty() ||
        address[0] != '/')
        return false;
    for (auto p = address.getCharPointer(); !p.isEmpty();) {
        const auto c = p.getAndAdvance();
        if (c < 0x21 || c > 0x7e || isPatternChar(c))
            return false;
    }
    return true;
}

namespace detail {

void writeOscSpecFields(const MessageSpec& spec, juce::DynamicObject& obj) {
    obj.setProperty("address", spec.address);
    obj.setProperty("argIndex", spec.argIndex);
}

bool readOscSpecFields(const juce::DynamicObject& obj, MessageSpec& spec) {
    if (spec.channel != 0 || spec.number != 0)
        return false;
    const auto addressVar = obj.getProperty("address");
    if (!addressVar.isString() || !isValidOscAddress(addressVar.toString()))
        return false;
    spec.address = addressVar.toString();

    const auto argVar = obj.getProperty("argIndex");
    if (argVar.isVoid()) {
        spec.argIndex = 0;
        return true;
    }
    if (!argVar.isInt())
        return false;
    const int arg = static_cast<int>(argVar);
    if (arg < 0 || arg > kMaxOscArgIndex)
        return false;
    spec.argIndex = arg;
    return true;
}

void writeInRange(const Control& control, juce::DynamicObject& obj) {
    if (control.message.type != MessageType::osc)
        return;
    auto* range = new juce::DynamicObject();
    range->setProperty("min", control.inRange.min);
    range->setProperty("max", control.inRange.max);
    obj.setProperty("inRange", juce::var(range));
}

bool readInRange(const juce::DynamicObject& obj, Control& control) {
    control.inRange.min = 0.0;
    control.inRange.max = 1.0;
    const auto rangeVar = obj.getProperty("inRange");
    if (rangeVar.isVoid() || control.message.type != MessageType::osc)
        return true;
    auto* range = rangeVar.getDynamicObject();
    return range != nullptr && readFinite(range->getProperty("min"), control.inRange.min) &&
           readFinite(range->getProperty("max"), control.inRange.max);
}

} // namespace detail

} // namespace synth
