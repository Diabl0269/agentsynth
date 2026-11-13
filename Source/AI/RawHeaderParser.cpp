#include "RawHeaderParser.h"

namespace synth {

juce::StringPairArray parseRawHttpHeaders(const juce::String& rawHeaders) {
    juce::StringPairArray headers;

    for (const auto& rawLine : juce::StringArray::fromLines(rawHeaders)) {
        const auto line = rawLine.trim();
        const int colon = line.indexOfChar(':');
        if (colon <= 0)
            continue;

        const auto key = line.substring(0, colon).trim();
        if (key.isNotEmpty())
            headers.set(key, line.substring(colon + 1).trim());
    }

    return headers;
}

} // namespace synth
