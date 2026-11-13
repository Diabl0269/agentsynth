#pragma once

#include <juce_core/juce_core.h>

namespace synth {

// Finds the `lame` MP3 encoder the user has installed. Agent Synth never bundles an encoder:
// MP3 export runs the user's own `lame` through juce::LAMEEncoderAudioFormat, so this is the one
// place that decides whether the MP3 choice in Export Audio is available.

// The directories searched by default, in order: every PATH entry, then the usual package-manager
// locations (Homebrew on Apple silicon and Intel, /usr/bin) that a GUI app's PATH often lacks.
juce::StringArray defaultLameSearchDirectories();

// The first executable file named `lame` (`lame.exe` on Windows) inside `directories`, or a
// non-existent juce::File when there is none. The directories are a parameter so tests can point
// it at a scratch folder instead of the machine's real install.
juce::File findLameExecutable(const juce::StringArray& directories);
juce::File findLameExecutable();

// Shown next to the disabled MP3 choice when no encoder was found.
juce::String lameInstallHint();

} // namespace synth
