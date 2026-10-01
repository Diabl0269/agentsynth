#include "UI/Layout/TabStripKeys.h"

#include <algorithm>

// Concern: which tab a tab-strip navigation key opens (see the header).

namespace synth::ui {

std::optional<int> tabStripKeyTarget(const juce::KeyPress& key, int current, int count) {
    if (count <= 0 || key.getModifiers().isAnyModifierKeyDown())
        return std::nullopt;
    const int last = count - 1;
    const int from = std::clamp(current, 0, last);
    if (key.isKeyCode(juce::KeyPress::leftKey))
        return std::max(0, from - 1);
    if (key.isKeyCode(juce::KeyPress::rightKey))
        return std::min(last, from + 1);
    if (key.isKeyCode(juce::KeyPress::homeKey))
        return 0;
    if (key.isKeyCode(juce::KeyPress::endKey))
        return last;
    return std::nullopt;
}

} // namespace synth::ui
