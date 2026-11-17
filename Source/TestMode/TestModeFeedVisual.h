#pragma once

#include "TestMode/TestModeCommands.h"
#include <juce_audio_processors/juce_audio_processors.h>

// TestModeFeedVisual.h: `feedVisual` (docs/development/test-mode.md#feedvisual). `feedVisual {id, kind:
// sine|noise|silence}` detaches the silent device's callback, then writes a burst of that signal into the module's
// analysis buffers (the scope's VisualBuffer and, for a Filter, its spectrum buffer) so a clip can film "a note
// sounds, then it goes quiet" without a source module; the display holds until `audioPump {run: true}`.
namespace synth::testmode {

enum class FeedKind { sine, noise, silence };

/** Writes `numSamples` of `kind` (`hz` and `amp` for a sine, `amp` for noise) into the module's visual buffers.
 *  Returns how many buffers it wrote (0: the module has none). Only for a module nothing renders: the buffers have one
 *  writer by contract, so the caller detaches the device callback first (the command does). */
int feedVisualBuffers(juce::AudioProcessor& module, FeedKind kind, double hz, float amp, int numSamples,
                      double sampleRate);

/** Adds feedVisual. */
void registerFeedVisualCommands(CommandTable& table);

} // namespace synth::testmode
