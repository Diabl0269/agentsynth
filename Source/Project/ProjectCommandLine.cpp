#include "Project/ProjectCommandLine.h"
#include "Plugin/Hosting/PluginScanService.h"
#include "ProjectBundle.h"

namespace synth {

std::optional<juce::File> projectBundleFromCommandLine(const juce::StringArray& args, const juce::File& workingDir) {
    // The scan child re-launches this binary with its own argument list; none of it is ever a project.
    if (args.contains(PluginScanService::kScanArgvFlag))
        return std::nullopt;

    for (const auto& raw : args) {
        const auto arg = raw.trim().unquoted();
        // A flag's value (macOS passes "-NSDocumentRevisionsDebugMode YES") fails the bundle test below,
        // so scanning past it is harmless.
        if (arg.isEmpty() || arg.startsWithChar('-'))
            continue;
        const auto candidate = workingDir.getChildFile(arg); // an absolute path ignores workingDir
        if (ProjectBundle::isBundle(candidate))
            return candidate;
    }
    return std::nullopt;
}

std::optional<juce::File> projectBundleFromCommandLine(const juce::String& commandLine, const juce::File& workingDir) {
    return projectBundleFromCommandLine(juce::StringArray::fromTokens(commandLine, true), workingDir);
}

} // namespace synth
