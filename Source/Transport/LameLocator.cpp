#include "LameLocator.h"

namespace synth {

juce::StringArray defaultLameSearchDirectories() {
    juce::StringArray dirs;
#if JUCE_WINDOWS
    constexpr auto kPathSeparator = ";";
#else
    constexpr auto kPathSeparator = ":";
#endif
    dirs.addTokens(juce::SystemStats::getEnvironmentVariable("PATH", {}), kPathSeparator, "\"");
#if !JUCE_WINDOWS
    dirs.add("/opt/homebrew/bin");
    dirs.add("/usr/local/bin");
    dirs.add("/usr/bin");
#endif
    dirs.trim();
    dirs.removeEmptyStrings();
    dirs.removeDuplicates(false);
    return dirs;
}

juce::File findLameExecutable(const juce::StringArray& directories) {
#if JUCE_WINDOWS
    const juce::String name = "lame.exe";
#else
    const juce::String name = "lame";
#endif
    for (const auto& dir : directories) {
        const auto candidate = juce::File(dir).getChildFile(name);
        if (candidate.existsAsFile())
            return candidate;
    }
    return {};
}

juce::File findLameExecutable() { return findLameExecutable(defaultLameSearchDirectories()); }

juce::String lameInstallHint() { return "Install lame to export MP3, e.g. brew install lame"; }

} // namespace synth
