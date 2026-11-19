#include "LocalPromptTrim.h"

namespace synth::projectgenerate {

const juce::StringArray& compactOmittedHeadings() {
    static const juce::StringArray headings{"Example 5:",         "Full songs",    "Sequencer and Poly Sequencer steps",
                                            "Song key",           "Sequencer key", "One player per part",
                                            "Pattern vocabulary", "Melodic leads", "Sound recipes",
                                            "Example 6:",         "Example 7:"};
    return headings;
}

juce::String omitSections(const juce::String& prompt, const juce::StringArray& headings, juce::StringArray* missing) {
    juce::StringArray lines;
    lines.addLines(prompt);
    juce::StringArray seen;
    juce::String out;
    bool omitting = false;
    for (int i = 0; i < lines.size(); ++i) {
        const juce::String& line = lines[i];
        if (line.startsWith("### ")) {
            omitting = false;
            const juce::String title = line.substring(4);
            for (const auto& heading : headings)
                if (title.startsWith(heading)) {
                    omitting = true;
                    seen.addIfNotAlreadyThere(heading);
                    break;
                }
        }
        if (!omitting)
            out << line << (i + 1 < lines.size() ? "\n" : "");
    }
    if (missing != nullptr)
        for (const auto& heading : headings)
            if (!seen.contains(heading))
                missing->add(heading);
    return out;
}

} // namespace synth::projectgenerate
