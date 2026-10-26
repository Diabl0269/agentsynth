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

/** The longest window renderStretchedWindow reads: rendering blocks the caller, about a second per minute of audio. */
constexpr double kMaxStretchWindowSeconds = 60.0;

/** Writes to `dest` (replacing it) a WAV of the `lengthSeconds` of `source` from `startSeconds`, played `tempoRatio`
 *  times faster (so round(window / ratio) samples) at the same pitch. Past the file's end reads as silence. False, with
 *  `dest` untouched, when the window is empty or over kMaxStretchWindowSeconds, the ratio is not positive, or the file
 *  cannot be read or written. Blocking. */
bool renderStretchedWindow(const juce::File& source, double startSeconds, double lengthSeconds, double tempoRatio,
                           const juce::File& dest);

} // namespace synth::soundbrowser
