#pragma once

// SectionStripSettings.h: the two Settings keys the Sections strip reads at use time, and the helpers that read them.
// Preferences > Timeline writes both; nothing is cached, so a change applies on the next gesture (the strip itself
// reappears or goes via TimelinePanelComponent::refreshSectionsStripSetting).

#include <juce_data_structures/juce_data_structures.h>

namespace synth::ui {

// "Timeline: show the Sections strip" (default on).
inline constexpr const char* kTimelineShowSectionsStripKey = "timelineShowSectionsStrip";
// "Timeline: placing a section covers the music where it lands" (default off: placing makes room instead).
inline constexpr const char* kTimelineSectionsCoverOnDropKey = "timelineSectionsCoverOnDrop";

inline bool sectionsStripShown(const juce::PropertiesFile* props) {
    return props == nullptr || props->getBoolValue(kTimelineShowSectionsStripKey, true);
}

inline bool sectionsCoverOnDrop(const juce::PropertiesFile* props) {
    return props != nullptr && props->getBoolValue(kTimelineSectionsCoverOnDropKey, false);
}

} // namespace synth::ui
