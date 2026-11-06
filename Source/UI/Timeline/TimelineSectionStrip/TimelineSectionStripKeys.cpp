// TimelineSectionStripKeys.cpp
//
// The Sections strip's keyboard. Not wired yet: the strip takes no keyboard focus and declines every key.

#include "UI/Timeline/TimelineSectionStrip/TimelineSectionStrip.h"

namespace synth::ui {

bool TimelineSectionStrip::keyPressed(const juce::KeyPress& key) {
    juce::ignoreUnused(key);
    return false;
}

} // namespace synth::ui
