// ControllerProfile's "protocol" key, kept out of RemoteModelJson.cpp (near its size cap). Same
// strict rule as every other enum there: absent means the default, an unknown spelling rejects the load.
#include "MidiRemote/ControllerProtocol.h"

namespace synth {

juce::String controllerProtocolToString(ControllerProtocol protocol) {
    switch (protocol) {
    case ControllerProtocol::mcu:
        return "mcu";
    case ControllerProtocol::generic:
        break;
    }
    return "generic";
}

bool controllerProtocolFromString(const juce::String& text, ControllerProtocol& out) {
    if (text == "generic")
        out = ControllerProtocol::generic;
    else if (text == "mcu")
        out = ControllerProtocol::mcu;
    else
        return false;
    return true;
}

void writeControllerProtocol(juce::DynamicObject& profile, ControllerProtocol protocol) {
    if (protocol != ControllerProtocol::generic)
        profile.setProperty("protocol", controllerProtocolToString(protocol));
}

bool readControllerProtocol(const juce::var& value, ControllerProtocol& out) {
    if (value.isVoid()) {
        out = ControllerProtocol::generic;
        return true;
    }
    if (!value.isString())
        return false;
    return controllerProtocolFromString(value.toString(), out);
}

} // namespace synth
