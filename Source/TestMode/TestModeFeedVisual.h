#pragma once

#include "TestMode/TestModeCommands.h"
#include <juce_audio_processors/juce_audio_processors.h>

// TestModeFeedVisual.h: `feedVisual` (docs/development/test-mode.md#feedvisual). A test-mode launch has no audio
// device, so a spectrum or scope that no source drives never moves. `feedVisual {id, kind: sine|noise|silence}` writes
// a burst of that signal into the module's analysis buffers (the scope's VisualBuffer and, for a Filter, its spectrum
// buffer) so a clip can film "a note sounds, then it goes quiet" without a source module or the audio pump.
namespace synth::testmode {

enum class FeedKind { sine, noise, silence };

/** Writes `numSamples` of `kind` (`hz` and `amp` for a sine, `amp` for noise) into the module's visual buffers.
 *  Returns how many buffers it wrote (0: the module has none). Only for a module nothing renders: the buffers have one
 *  writer by contract, so this must not run beside the audio thread or the pump (the command checks). */
int feedVisualBuffers(juce::AudioProcessor& module, FeedKind kind, double hz, float amp, int numSamples,
                      double sampleRate);

/** Adds feedVisual. */
void registerFeedVisualCommands(CommandTable& table);

} // namespace synth::testmode
