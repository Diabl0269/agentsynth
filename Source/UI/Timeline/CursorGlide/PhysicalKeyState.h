#pragma once

#include <optional>

// PhysicalKeyState.h: whether an arrow key is physically held, read from the OS rather than from
// juce::KeyPress::isKeyCurrentlyDown (docs/timeline/transport.md#gliding-the-cursor).

namespace synth::ui {

enum class ArrowKey { Left, Right, Up, Down };

/** The arrow key's hardware state on macOS; std::nullopt on other platforms (use JUCE there). */
std::optional<bool> physicalArrowKeyDown(ArrowKey key);

} // namespace synth::ui
