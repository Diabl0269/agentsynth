#pragma once

// A stretched copy of a loop on disk, for the places that take a file (an audio track). The copy is cached per session
// in the temp folder, so dropping the same loop at the same tempo twice renders once.

#include "SoundBrowser/LoopStretch.h"
#include <juce_core/juce_core.h>

namespace synth::soundbrowser {

/** Where stretched copies are written: "Agent Synth Stretched Loops" in the system temp folder. */
juce::File stretchedLoopCacheFolder();

/** A WAV of `source` stretched to `target` in `cacheFolder`, rendered now (blocking) unless the same copy is already
 *  there. `source` itself when nothing needs stretching, the file is longer than SoundPreviewPlayer::kMaxSeconds, or it
 *  cannot be read or written. Call it on a thread that may block for a second or so. */
juce::File stretchedLoopFile(const juce::File& source, const LoopTarget& target,
                             const juce::File& cacheFolder = stretchedLoopCacheFolder());

} // namespace synth::soundbrowser
