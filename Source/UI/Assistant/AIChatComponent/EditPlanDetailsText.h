#pragma once

#include <juce_core/juce_core.h>

namespace synth {

// juce::TextEditor lays out everything it is given synchronously, and breaks a long unbroken run
// glyph by glyph; a full song's plan was megabytes of JSON, which froze the UI thread. The panel
// shows only the plan's summary, and even that is a bounded head with no run of characters without
// whitespace longer than kMaxTokenChars.
inline constexpr int kMaxDetailsChars = 20000;
inline constexpr int kMaxTokenChars = 120;
// Inserts a newline into any run of non-whitespace characters longer than kMaxTokenChars.
inline juce::String breakLongTokens(const juce::String& text) {
    juce::String out;
    out.preallocateBytes(text.getNumBytesAsUTF8() + 64);
    int run = 0;
    for (auto it = text.getCharPointer(); !it.isEmpty();) {
        const juce::juce_wchar c = it.getAndAdvance();
        run = juce::CharacterFunctions::isWhitespace(c) ? 0 : run + 1;
        if (run > kMaxTokenChars) {
            out << '\n';
            run = 1;
        }
        out << juce::String::charToString(c);
    }
    return out;
}

// The summary lines, cut to kMaxDetailsChars at a line end with a plain note of how much was left out.
inline juce::String buildEditPlanDetailsText(const juce::String& summary) {
    if (summary.isEmpty())
        return "No further details for this plan.";
    if (summary.length() <= kMaxDetailsChars)
        return breakLongTokens(summary);

    int cut = kMaxDetailsChars;
    const int lineEnd = summary.substring(0, cut).lastIndexOfChar('\n');
    if (lineEnd > kMaxDetailsChars / 2)
        cut = lineEnd;
    const int leftKb = juce::jmax(1, juce::roundToInt(static_cast<float>(summary.length() - cut) / 1024.0f));
    return breakLongTokens(summary.substring(0, cut)) + "\n\n... " + juce::String(leftKb) + " KB more not shown";
}

} // namespace synth
