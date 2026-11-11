#pragma once

#include "TestMode/TestModeCommands.h"
#include "TestMode/TestModePresentedFrame.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

// TestModePopWatch.h: the no-pop check (docs/development/test-mode.md#no-pop-check). `popWatchStart` records what the
// window shows before an action and every frame the peer presents after it; `popWatchCheck` says whether the change
// arrived in one frame instead of animating.
namespace synth::testmode {

struct PopVerdict {
    bool changed = false;        // the window differs from before, outside the ignored areas
    bool pop = false;            // the change landed in one frame
    juce::Rectangle<int> region; // the box around the changed pixels
    int regionPixels = 0;
    int firstFrame = -1;        // index of the first captured frame that shows the change, -1 when none did
    int intermediateFrames = 0; // captured frames that show part of the change
};

/** Compares `before` and `final` outside `ignore`; when they differ, finds the first of `frames` (in time order) that
 *  differs from `before` inside the change and calls it a pop when it already equals `final` there, or when no frame
 *  shows the change at all. Images of other sizes are skipped. */
PopVerdict classifyPop(const juce::Image& before, const std::vector<juce::Image>& frames, const juce::Image& final,
                       const juce::RectangleList<int>& ignore, int tolerance = kStaleTolerance);

/** Adds popWatchStart and popWatchCheck. */
void registerPopWatchCommands(CommandTable& table);

} // namespace synth::testmode
