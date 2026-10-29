// Concern: the test-mode wire format -- parsing a request line and formatting a reply, nothing else.

#include "TestMode/TestModeProtocol.h"

namespace synth::testmode {

namespace {
juce::String reply(const juce::var& id, bool ok, const juce::Identifier& payloadKey, const juce::var& payload) {
    auto* object = new juce::DynamicObject();
    juce::var out(object);
    object->setProperty("id", id);
    object->setProperty("ok", ok);
    object->setProperty(payloadKey, payload);
    return juce::JSON::toString(out, /*allOnOneLine=*/true);
}
} // namespace

ParsedRequest parseRequestLine(const juce::String& line) {
    ParsedRequest parsed;
    juce::var json;
    if (!juce::JSON::parse(line, json).wasOk()) {
        parsed.error = "request is not valid JSON";
        return parsed;
    }
    auto* object = json.getDynamicObject();
    if (object == nullptr) {
        parsed.error = "request must be a JSON object";
        return parsed;
    }
    parsed.id = object->getProperty("id");
    const auto cmd = object->getProperty("cmd");
    if (!cmd.isString() || cmd.toString().isEmpty()) {
        parsed.error = "request needs a string \"cmd\"";
        return parsed;
    }
    Request request;
    request.id = parsed.id;
    request.cmd = cmd.toString();
    request.args = object->hasProperty("args") && object->getProperty("args").getDynamicObject() != nullptr
                       ? object->getProperty("args")
                       : juce::var(new juce::DynamicObject());
    if (object->hasProperty("args") && object->getProperty("args").getDynamicObject() == nullptr &&
        !object->getProperty("args").isVoid()) {
        parsed.error = "\"args\" must be a JSON object";
        return parsed;
    }
    request.timeoutMs = juce::jmax(0, (int)object->getProperty("timeoutMs"));
    parsed.request = std::move(request);
    return parsed;
}

juce::String formatOk(const juce::var& id, const juce::var& result) { return reply(id, true, "result", result); }

juce::String formatError(const juce::var& id, const juce::String& error) { return reply(id, false, "error", error); }

} // namespace synth::testmode
