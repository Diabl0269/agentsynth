#pragma once

#include <juce_core/juce_core.h>
#include <optional>

namespace synth {

/** The project bundle a command line asks the app to open: the first argument that is not a flag (nothing
 *  starting with '-') and names a `.agsproj` bundle (`ProjectBundle::isBundle`). A relative path resolves
 *  against `workingDir`. Anything else -- no arguments, flags only, a path that is not a bundle, the
 *  out-of-process plugin scan's arguments -- yields nullopt, so a stray argument never opens a window's
 *  worth of trouble. Pure: touches the file system only to test the candidate. */
std::optional<juce::File>
projectBundleFromCommandLine(const juce::StringArray& args,
                             const juce::File& workingDir = juce::File::getCurrentWorkingDirectory());

/** The same for a raw command-line string, as `JUCEApplication::anotherInstanceStarted` delivers it (a path
 *  containing spaces arrives in double quotes). */
std::optional<juce::File>
projectBundleFromCommandLine(const juce::String& commandLine,
                             const juce::File& workingDir = juce::File::getCurrentWorkingDirectory());

} // namespace synth
