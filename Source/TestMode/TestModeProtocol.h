#pragma once

#include <juce_core/juce_core.h>
#include <optional>

// TestModeProtocol.h: the wire format of the in-app test mode (docs/development/test-mode.md). One JSON object per
// line in each direction. Pure and headless: no sockets, no components.
namespace synth::testmode {

/** One parsed request line: {"id":1,"cmd":"click","args":{...},"timeoutMs":5000}. `id` is echoed back untouched. */
struct Request {
    juce::var id;
    juce::String cmd;
    juce::var args;    // an object; empty object when the line had none
    int timeoutMs = 0; // 0 = the command's own default
};

/** A parsed line, or the reason it is not a request. `id` is whatever could be read of it, so even a bad request
 *  gets an answer the client can match up. */
struct ParsedRequest {
    std::optional<Request> request;
    juce::String error;
    juce::var id;
};

/** Never throws: malformed JSON, a non-object, a missing or non-string "cmd" all come back as an `error`. */
ParsedRequest parseRequestLine(const juce::String& line);

/** {"id":..,"ok":true,"result":..} on a single line, without the trailing newline. */
juce::String formatOk(const juce::var& id, const juce::var& result);

/** {"id":..,"ok":false,"error":".."} on a single line, without the trailing newline. */
juce::String formatError(const juce::var& id, const juce::String& error);

} // namespace synth::testmode
