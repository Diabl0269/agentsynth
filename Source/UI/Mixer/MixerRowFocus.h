#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// MixerRowFocus.h (docs/mixer/panel.md#keyboard-navigation-and-accessibility): which insert, send or EQ
// row the mixer panel's keyboard focus sits on (the EQ section is one row, index 0). Keyboard row focus is state of the
// panel, never real focus on a child, so the panel stays the mixer's single focusable leaf.
namespace synth::ui {

enum class MixerRowKind { Insert, Send, Eq };

struct MixerRowRef {
    MixerRowKind kind = MixerRowKind::Insert;
    int index = 0;

    bool operator==(const MixerRowRef& other) const noexcept { return kind == other.kind && index == other.index; }
};

} // namespace synth::ui
