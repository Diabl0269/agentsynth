#pragma once

namespace synth::ui {

// Toast when a clip moved, pasted or duplicated past the end of its section placement was trimmed to it.
inline constexpr const char* kClipEndTrimmedBySectionToast =
    "This clip now ends at its section's end. Hold Option to make the section longer instead.";
// Toast when an Option copy-drag or an Option paste made the section longer so the whole clip fits.
inline constexpr const char* kSectionGrownForClipToast = "Section made longer to fit the clip.";
// Toast when a drag (right edge, or left edge) was held at its section placement's edge.
inline constexpr const char* kClipEdgeHeldBySectionToast = "To make this clip longer, make the section longer first.";

} // namespace synth::ui
