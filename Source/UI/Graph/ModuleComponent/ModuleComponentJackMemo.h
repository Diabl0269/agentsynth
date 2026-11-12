#pragma once

// ModuleComponentJackMemo.h
//
// What one ModuleComponent::JackLayoutPass has already worked out for a card: its modulation targets, its drawn jack
// column and each visible input jack's knob anchor. Kept out of ModuleComponent.h so the card header stays thin
// (docs/architecture/graph-queries.md#the-shared-forms).

#include "Modules/ModuleBase.h"
#include <juce_graphics/juce_graphics.h>
#include <optional>
#include <vector>

struct ModuleComponentJackMemo {
    juce::uint32 pass = 0; // the JackLayoutPass these answers belong to; 0: none
    bool targetsKnown = false;
    std::vector<ModulationTarget> targets;
    bool drawnKnown = false;
    std::vector<int> drawn;
    std::vector<std::optional<std::optional<juce::Point<float>>>> anchors; // per visible input; outer empty: not asked

    /** Forgets every answer unless they belong to `activePass`. */
    void beginPass(juce::uint32 activePass) {
        if (pass == activePass)
            return;
        *this = {};
        pass = activePass;
    }
};
