#pragma once

// Paints a keyboard whose every semitone has an equal column (the sampler's card strip and its map window, whose blocks
// line up with the keys). The white keys form one unbroken band under the black ones, split at each black key's centre
// and at E-F and B-C, and the black keys sit on top at the shared height (PianoKeyShape.h). Lit keys take the accent.

#include "UI/Layout/PianoKeyShape.h"
#include "UI/Theme/Theme.h"
#include <bitset>
#include <juce_graphics/juce_graphics.h>

namespace synth::ui::pianokeys {

inline void paintChromaticKeys(juce::Graphics& g, juce::Rectangle<float> area, int firstNote, int lastNote,
                               const std::bitset<128>& lit, const synth::theme::Colors& colors) {
    constexpr int kOctave = 12;
    const float keyWidth = area.getWidth() / (float)(lastNote - firstNote + 1);
    const float blackHeight = area.getHeight() * kBlackKeyHeightShare;
    const auto columnOf = [&](int note) { return area.getX() + (float)(note - firstNote) * keyWidth; };
    g.setColour(colors.pianoKeyWhite);
    g.fillRect(area);
    for (int note = firstNote; note <= lastNote; ++note) {
        const float x = columnOf(note);
        if (isBlackPitchClass(note % kOctave)) {
            g.setColour(colors.pianoKeyBlack);
            g.fillRect(juce::Rectangle<float>(x, area.getY(), keyWidth, blackHeight));
            g.setColour(colors.border);
            g.fillRect(juce::Rectangle<float>(x + keyWidth * 0.5f - 0.25f, area.getY() + blackHeight, 0.5f,
                                              area.getHeight() - blackHeight));
        } else if (note % kOctave == 0 || note % kOctave == 5) {
            g.setColour(colors.border);
            g.fillRect(juce::Rectangle<float>(x, area.getY(), 0.5f, area.getHeight()));
        }
    }
    g.setColour(colors.accent);
    for (int note = firstNote; note <= lastNote; ++note)
        if (lit.test((size_t)note))
            g.fillRect(juce::Rectangle<float>(columnOf(note), area.getY(), keyWidth,
                                              isBlackPitchClass(note % kOctave) ? blackHeight : area.getHeight()));
}

} // namespace synth::ui::pianokeys
