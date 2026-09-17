// Concern: serialisation (toVar/fromVar JSON dialect) for every type in RemoteModel.h.
#include "RemoteModel.h"

#include <cmath>
#include <limits>

namespace synth {

namespace {

// -- juce::var readers ------------------------------------------------------------------------
// Same loader rule as TimelineDocSerialization.cpp: a PRESENT property must be well-typed and in
// range or the whole load fails; these are file-local (like that file's own readers) since
// nothing else in Core needs them.

bool readString(const juce::var& v, juce::String& out) {
    if (!v.isString())
        return false;
    out = v.toString();
    return true;
}

bool readInt(const juce::var& v, int& out) {
    if (v.isInt()) {
        out = static_cast<int>(v);
        return true;
    }
    if (v.isInt64()) {
        const auto wide = static_cast<std::int64_t>(static_cast<juce::int64>(v));
        if (wide < std::numeric_limits<int>::min() || wide > std::numeric_limits<int>::max())
            return false;
        out = static_cast<int>(wide);
        return true;
    }
    return false;
}

bool readOptionalInt(const juce::var& v, int& out) { return v.isVoid() || readInt(v, out); }

bool readDouble(const juce::var& v, double& out) {
    if (v.isDouble() || v.isInt() || v.isInt64()) {
        out = static_cast<double>(v);
        return true;
    }
    return false;
}

bool readBool(const juce::var& v, bool& out) {
    if (!v.isBool())
        return false;
    out = static_cast<bool>(v);
    return true;
}

// -- Enum <-> doc-exact camelCase string ------------------------------------------------------
// docs/midi_remote.md §5 names every enumerator exactly this way; a string this build doesn't
// recognise is a hard failure, never a silent default (§7 "never partially applies").

const char* toString(MessageType t) {
    switch (t) {
    case MessageType::cc:
        return "cc";
    case MessageType::note:
        return "note";
    case MessageType::pitchBend:
        return "pitchBend";
    case MessageType::channelPressure:
        return "channelPressure";
    case MessageType::programChange:
        return "programChange";
    }
    return "cc";
}

bool messageTypeFromString(const juce::String& s, MessageType& out) {
    if (s == "cc") {
        out = MessageType::cc;
        return true;
    }
    if (s == "note") {
        out = MessageType::note;
        return true;
    }
    if (s == "pitchBend") {
        out = MessageType::pitchBend;
        return true;
    }
    if (s == "channelPressure") {
        out = MessageType::channelPressure;
        return true;
    }
    if (s == "programChange") {
        out = MessageType::programChange;
        return true;
    }
    return false;
}

const char* toString(ControlKind k) {
    switch (k) {
    case ControlKind::knob:
        return "knob";
    case ControlKind::fader:
        return "fader";
    case ControlKind::button:
        return "button";
    case ControlKind::pad:
        return "pad";
    case ControlKind::encoder:
        return "encoder";
    case ControlKind::wheel:
        return "wheel";
    }
    return "knob";
}

bool controlKindFromString(const juce::String& s, ControlKind& out) {
    if (s == "knob") {
        out = ControlKind::knob;
        return true;
    }
    if (s == "fader") {
        out = ControlKind::fader;
        return true;
    }
    if (s == "button") {
        out = ControlKind::button;
        return true;
    }
    if (s == "pad") {
        out = ControlKind::pad;
        return true;
    }
    if (s == "encoder") {
        out = ControlKind::encoder;
        return true;
    }
    if (s == "wheel") {
        out = ControlKind::wheel;
        return true;
    }
    return false;
}

const char* toString(Encoding e) {
    switch (e) {
    case Encoding::abs7:
        return "abs7";
    case Encoding::relTwos:
        return "relTwos";
    case Encoding::relBinOffset:
        return "relBinOffset";
    case Encoding::relSignMag:
        return "relSignMag";
    }
    return "abs7";
}

bool encodingFromString(const juce::String& s, Encoding& out) {
    if (s == "abs7") {
        out = Encoding::abs7;
        return true;
    }
    if (s == "relTwos") {
        out = Encoding::relTwos;
        return true;
    }
    if (s == "relBinOffset") {
        out = Encoding::relBinOffset;
        return true;
    }
    if (s == "relSignMag") {
        out = Encoding::relSignMag;
        return true;
    }
    return false;
}

const char* toString(ButtonMode m) {
    switch (m) {
    case ButtonMode::momentary:
        return "momentary";
    case ButtonMode::toggle:
        return "toggle";
    }
    return "momentary";
}

bool buttonModeFromString(const juce::String& s, ButtonMode& out) {
    if (s == "momentary") {
        out = ButtonMode::momentary;
        return true;
    }
    if (s == "toggle") {
        out = ButtonMode::toggle;
        return true;
    }
    return false;
}

const char* toString(Takeover t) {
    switch (t) {
    case Takeover::jump:
        return "jump";
    case Takeover::pickup:
        return "pickup";
    case Takeover::scale:
        return "scale";
    case Takeover::useDefault:
        return "default";
    }
    return "default";
}

bool takeoverFromString(const juce::String& s, Takeover& out) {
    if (s == "jump") {
        out = Takeover::jump;
        return true;
    }
    if (s == "pickup") {
        out = Takeover::pickup;
        return true;
    }
    if (s == "scale") {
        out = Takeover::scale;
        return true;
    }
    if (s == "default") {
        out = Takeover::useDefault;
        return true;
    }
    return false;
}

// -- List helpers, factored out so ControllerProfile::fromVar / MidiRemoteProjectDoc::fromVar
// stay well under the function-size cap. Both lists are required arrays (this dialect always
// writes them, even when empty) rather than optional — see toVar below.

bool readControlList(const juce::var& v, std::vector<Control>& out) {
    auto* arr = v.getArray();
    if (arr == nullptr)
        return false;
    out.clear();
    out.reserve(static_cast<size_t>(arr->size()));
    for (const auto& item : *arr) {
        Control c;
        if (!Control::fromVar(item, c))
            return false;
        out.push_back(std::move(c));
    }
    return true;
}

bool readAssignmentList(const juce::var& v, std::vector<Assignment>& out) {
    auto* arr = v.getArray();
    if (arr == nullptr)
        return false;
    out.clear();
    out.reserve(static_cast<size_t>(arr->size()));
    for (const auto& item : *arr) {
        Assignment a;
        if (!Assignment::fromVar(item, a))
            return false;
        out.push_back(std::move(a));
    }
    return true;
}

} // namespace

// -------------------------------------------------------------------------------- MessageSpec --

juce::var MessageSpec::toVar() const {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("type", toString(type));
    obj->setProperty("channel", channel);
    obj->setProperty("number", number);
    return juce::var(obj);
}

bool MessageSpec::fromVar(const juce::var& v, MessageSpec& out) {
    auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return false;

    juce::String typeStr;
    MessageType type = MessageType::cc;
    if (!readString(obj->getProperty("type"), typeStr) || !messageTypeFromString(typeStr, type))
        return false;

    int channel = 0;
    if (!readInt(obj->getProperty("channel"), channel) || channel < 0 || channel > 16)
        return false;

    int number = 0;
    if (!readInt(obj->getProperty("number"), number) || number < 0 || number > 127)
        return false;

    out.type = type;
    out.channel = channel;
    out.number = number;
    return true;
}

// ----------------------------------------------------------------------------------- Control --

juce::var Control::toVar() const {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("id", id);
    obj->setProperty("name", name);
    obj->setProperty("kind", toString(kind));
    obj->setProperty("message", message.toVar());
    obj->setProperty("encoding", toString(encoding));
    obj->setProperty("buttonMode", toString(buttonMode));

    auto* layoutObj = new juce::DynamicObject();
    layoutObj->setProperty("col", layout.col);
    layoutObj->setProperty("row", layout.row);
    obj->setProperty("layout", juce::var(layoutObj));

    return juce::var(obj);
}

bool Control::fromVar(const juce::var& v, Control& out) {
    auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return false;

    Control parsed;
    if (!readString(obj->getProperty("id"), parsed.id) || parsed.id.isEmpty())
        return false;
    if (!readString(obj->getProperty("name"), parsed.name))
        return false;

    juce::String kindStr;
    if (!readString(obj->getProperty("kind"), kindStr) || !controlKindFromString(kindStr, parsed.kind))
        return false;

    if (!MessageSpec::fromVar(obj->getProperty("message"), parsed.message))
        return false;

    juce::String encStr;
    if (!readString(obj->getProperty("encoding"), encStr) || !encodingFromString(encStr, parsed.encoding))
        return false;

    juce::String buttonModeStr;
    if (!readString(obj->getProperty("buttonMode"), buttonModeStr) ||
        !buttonModeFromString(buttonModeStr, parsed.buttonMode))
        return false;

    auto* layoutObj = obj->getProperty("layout").getDynamicObject();
    if (layoutObj == nullptr)
        return false;
    if (!readInt(layoutObj->getProperty("col"), parsed.layout.col) ||
        !readInt(layoutObj->getProperty("row"), parsed.layout.row))
        return false;

    out = parsed;
    return true;
}

// ------------------------------------------------------------------------------------ Target --

juce::var Target::toVar() const {
    auto* obj = new juce::DynamicObject();
    if (kind == Kind::parameter) {
        auto* p = new juce::DynamicObject();
        p->setProperty("nodeUuid", parameter.nodeUuid);
        p->setProperty("paramId", parameter.paramId);
        p->setProperty("paramIndexHint", parameter.paramIndexHint);
        obj->setProperty("parameter", juce::var(p));
    } else {
        auto* a = new juce::DynamicObject();
        a->setProperty("actionId", action.actionId);
        obj->setProperty("action", juce::var(a));
    }
    return juce::var(obj);
}

bool Target::fromVar(const juce::var& v, Target& out) {
    auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return false;

    const bool hasParameter = obj->hasProperty("parameter");
    const bool hasAction = obj->hasProperty("action");
    // Reject BOTH present or NEITHER present — exactly one is a Target (docs/midi_remote.md §5).
    if (hasParameter == hasAction)
        return false;

    Target parsed;
    if (hasParameter) {
        auto* p = obj->getProperty("parameter").getDynamicObject();
        if (p == nullptr)
            return false;

        Target::Parameter param;
        if (!readString(p->getProperty("nodeUuid"), param.nodeUuid) || param.nodeUuid.isEmpty())
            return false;
        if (!readString(p->getProperty("paramId"), param.paramId) || param.paramId.isEmpty())
            return false;
        if (!readOptionalInt(p->getProperty("paramIndexHint"), param.paramIndexHint))
            return false;

        parsed.kind = Target::Kind::parameter;
        parsed.parameter = param;
    } else {
        auto* a = obj->getProperty("action").getDynamicObject();
        if (a == nullptr)
            return false;

        Target::Action act;
        if (!readString(a->getProperty("actionId"), act.actionId) || act.actionId.isEmpty())
            return false;

        parsed.kind = Target::Kind::action;
        parsed.action = act;
    }

    out = parsed;
    return true;
}

// -------------------------------------------------------------------------------- Assignment --

juce::var Assignment::toVar() const {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("id", id);

    auto* controlObj = new juce::DynamicObject();
    controlObj->setProperty("profileId", control.profileId);
    controlObj->setProperty("controlId", control.controlId);
    obj->setProperty("control", juce::var(controlObj));

    obj->setProperty("spec", spec.toVar());
    obj->setProperty("specEncoding", toString(specEncoding));
    obj->setProperty("specButtonMode", toString(specButtonMode));
    obj->setProperty("specControlName", specControlName);

    obj->setProperty("target", target.toVar());
    obj->setProperty("takeover", toString(takeover));

    auto* rangeObj = new juce::DynamicObject();
    rangeObj->setProperty("min", range.min);
    rangeObj->setProperty("max", range.max);
    obj->setProperty("range", juce::var(rangeObj));

    obj->setProperty("enabled", enabled);

    return juce::var(obj);
}

bool Assignment::fromVar(const juce::var& v, Assignment& out) {
    auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return false;

    Assignment parsed;
    if (!readString(obj->getProperty("id"), parsed.id) || parsed.id.isEmpty())
        return false;

    auto* controlObj = obj->getProperty("control").getDynamicObject();
    if (controlObj == nullptr)
        return false;
    if (!readString(controlObj->getProperty("profileId"), parsed.control.profileId) ||
        parsed.control.profileId.isEmpty() ||
        !readString(controlObj->getProperty("controlId"), parsed.control.controlId) ||
        parsed.control.controlId.isEmpty())
        return false;

    if (!MessageSpec::fromVar(obj->getProperty("spec"), parsed.spec))
        return false;

    juce::String encStr;
    if (!readString(obj->getProperty("specEncoding"), encStr) || !encodingFromString(encStr, parsed.specEncoding))
        return false;

    juce::String buttonModeStr;
    if (!readString(obj->getProperty("specButtonMode"), buttonModeStr) ||
        !buttonModeFromString(buttonModeStr, parsed.specButtonMode))
        return false;

    if (!readString(obj->getProperty("specControlName"), parsed.specControlName))
        return false;

    if (!Target::fromVar(obj->getProperty("target"), parsed.target))
        return false;

    juce::String takeoverStr;
    if (!readString(obj->getProperty("takeover"), takeoverStr) || !takeoverFromString(takeoverStr, parsed.takeover))
        return false;

    auto* rangeObj = obj->getProperty("range").getDynamicObject();
    if (rangeObj == nullptr)
        return false;
    if (!readDouble(rangeObj->getProperty("min"), parsed.range.min) ||
        !readDouble(rangeObj->getProperty("max"), parsed.range.max))
        return false;
    if (!std::isfinite(parsed.range.min) || !std::isfinite(parsed.range.max))
        return false;

    if (!readBool(obj->getProperty("enabled"), parsed.enabled))
        return false;

    out = parsed;
    return true;
}

// --------------------------------------------------------------------------- ControllerProfile --

juce::var ControllerProfile::toVar() const {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("version", version);
    obj->setProperty("id", id);
    obj->setProperty("name", name);

    auto* inputObj = new juce::DynamicObject();
    inputObj->setProperty("identifier", input.identifier);
    inputObj->setProperty("name", input.name);
    obj->setProperty("input", juce::var(inputObj));

    if (hasOutput) {
        auto* outputObj = new juce::DynamicObject();
        outputObj->setProperty("identifier", output.identifier);
        outputObj->setProperty("name", output.name);
        obj->setProperty("output", juce::var(outputObj));
    } else {
        obj->setProperty("output", juce::var());
    }

    obj->setProperty("passMapped", passMapped);

    juce::Array<juce::var> controlArr;
    for (const auto& c : controls)
        controlArr.add(c.toVar());
    obj->setProperty("controls", controlArr);

    juce::Array<juce::var> actionArr;
    for (const auto& a : actions)
        actionArr.add(a.toVar());
    obj->setProperty("actions", actionArr);

    return juce::var(obj);
}

bool ControllerProfile::fromVar(const juce::var& state) {
    auto* obj = state.getDynamicObject();
    if (obj == nullptr)
        return false;

    // §7: version must equal exactly 1; a higher (or otherwise wrong/missing) version is refused
    // visibly rather than coerced — same strict, present-and-exact convention TimelineDoc::fromVar
    // uses (there is no pre-versioning midiRemote data to be forward-compatible with).
    int parsedVersion = 0;
    if (!readInt(obj->getProperty("version"), parsedVersion) || parsedVersion != 1)
        return false;

    ControllerProfile parsed;
    parsed.version = parsedVersion;

    if (!readString(obj->getProperty("id"), parsed.id) || parsed.id.isEmpty())
        return false;
    if (!readString(obj->getProperty("name"), parsed.name))
        return false;

    auto* inputObj = obj->getProperty("input").getDynamicObject();
    if (inputObj == nullptr)
        return false;
    if (!readString(inputObj->getProperty("identifier"), parsed.input.identifier) ||
        !readString(inputObj->getProperty("name"), parsed.input.name))
        return false;

    const juce::var outputVar = obj->getProperty("output");
    if (outputVar.isVoid()) {
        parsed.hasOutput = false;
    } else {
        auto* outputObj = outputVar.getDynamicObject();
        if (outputObj == nullptr)
            return false;
        if (!readString(outputObj->getProperty("identifier"), parsed.output.identifier) ||
            !readString(outputObj->getProperty("name"), parsed.output.name))
            return false;
        parsed.hasOutput = true;
    }

    if (!readBool(obj->getProperty("passMapped"), parsed.passMapped))
        return false;

    if (!readControlList(obj->getProperty("controls"), parsed.controls))
        return false;

    // "Reject a profile whose controls share a MessageSpec key" (docs/midi_remote.md §5's rule,
    // ticket FRO124): an O(n^2) scan is fine at controller-surface scale (tens of controls).
    for (size_t i = 0; i < parsed.controls.size(); ++i)
        for (size_t j = i + 1; j < parsed.controls.size(); ++j)
            if (parsed.controls[i].message == parsed.controls[j].message)
                return false;

    if (!readAssignmentList(obj->getProperty("actions"), parsed.actions))
        return false;

    *this = std::move(parsed);
    return true;
}

// ----------------------------------------------------------------------- MidiRemoteProjectDoc --

juce::var MidiRemoteProjectDoc::toVar() const {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("version", version);

    juce::Array<juce::var> assignmentArr;
    for (const auto& a : assignments)
        assignmentArr.add(a.toVar());
    obj->setProperty("assignments", assignmentArr);

    juce::Array<juce::var> controllerArr;
    for (const auto& c : controllers) {
        auto* cObj = new juce::DynamicObject();
        cObj->setProperty("profileId", c.profileId);
        cObj->setProperty("name", c.name);
        controllerArr.add(juce::var(cObj));
    }
    obj->setProperty("controllers", controllerArr);

    return juce::var(obj);
}

bool MidiRemoteProjectDoc::fromVar(const juce::var& state) {
    auto* obj = state.getDynamicObject();
    if (obj == nullptr)
        return false;

    // Same strict, present-and-exact version convention as ControllerProfile::fromVar above.
    int parsedVersion = 0;
    if (!readInt(obj->getProperty("version"), parsedVersion) || parsedVersion != 1)
        return false;

    MidiRemoteProjectDoc parsed;
    parsed.version = parsedVersion;

    if (!readAssignmentList(obj->getProperty("assignments"), parsed.assignments))
        return false;

    auto* controllerArr = obj->getProperty("controllers").getArray();
    if (controllerArr == nullptr)
        return false;
    parsed.controllers.reserve(static_cast<size_t>(controllerArr->size()));
    for (const auto& item : *controllerArr) {
        auto* cObj = item.getDynamicObject();
        if (cObj == nullptr)
            return false;

        MidiRemoteProjectDoc::ControllerRef ref;
        if (!readString(cObj->getProperty("profileId"), ref.profileId) || ref.profileId.isEmpty())
            return false;
        if (!readString(cObj->getProperty("name"), ref.name))
            return false;

        parsed.controllers.push_back(std::move(ref));
    }

    *this = std::move(parsed);
    return true;
}

} // namespace synth
