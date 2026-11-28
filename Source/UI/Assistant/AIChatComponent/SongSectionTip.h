#pragma once

#include "AI/SongSteps/SongSteps.h"

namespace synth::songsteps {

// The tooltip of one block of the plan card's Sections strip: short lines a newcomer can read, the shared tooltip
// window shows each on its own line. Line one is the name and length, line two says what the energy number means (the
// one table below), line three which tracks play (left out when none do) and the last what the plan says the section
// does (left out when empty). Every line starts with a capital.
inline juce::String energyTipLine(int energy) {
    const int level = juce::jlimit(1, 5, energy);
    const juce::String head = "Energy: " + juce::String(level) + " of 5 (";
    switch (level) {
    case 1:
        return head + "1 is the quietest, 5 the fullest)";
    case 5:
        return head + "5 is the fullest, 1 the quietest)";
    default: {
        static const char* words[] = {"", "", "quiet", "medium", "high"};
        return head + words[level] + "; 1 is the quietest, 5 the fullest)";
    }
    }
}

inline juce::String sectionTipText(const SongPlan& plan, const PlanSection& section) {
    juce::StringArray lines;
    lines.add(section.name + juce::String::fromUTF8(" \xC2\xB7 ") + juce::String(section.bars) + " bars");
    lines.add(energyTipLine(section.energy));
    juce::StringArray players;
    for (const auto& track : plan.tracks)
        for (const auto& in : track.sections)
            if (in == section.name)
                players.add(track.name);
    if (!players.isEmpty())
        lines.add("Plays: " + players.joinIntoString(", "));
    if (section.description.isNotEmpty()) {
        // The plan's text starts a line, so its first letter is a capital; the rest is left as written.
        const auto first =
            juce::CharacterFunctions::toUpperCase(section.description[0]); // not-ui-text: first letter only
        lines.add(juce::String::charToString(first) + section.description.substring(1));
    }
    return lines.joinIntoString("\n");
}

} // namespace synth::songsteps
