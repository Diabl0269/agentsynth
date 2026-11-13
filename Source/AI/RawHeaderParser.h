#pragma once

#include <juce_core/juce_core.h>

namespace synth {

/**
 * Parses a block of raw HTTP response headers ("Name: value" lines separated by CRLF or LF, the
 * status line optional) into a juce::StringPairArray.
 *
 * Platform independent so it is unit-tested everywhere; the Windows WinHTTP transport feeds it
 * WINHTTP_QUERY_RAW_HEADERS_CRLF. It matches what the libcurl header callbacks do line by line:
 * names and values are trimmed, names keep the casing the server sent, a line with no colon (the
 * status line) or an empty name is skipped, and a repeated name keeps its last value.
 */
juce::StringPairArray parseRawHttpHeaders(const juce::String& rawHeaders);

} // namespace synth
