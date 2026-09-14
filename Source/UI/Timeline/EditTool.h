#pragma once

#include <array>
#include <optional>

namespace synth::ui {

/**
 * @brief The user-selectable edit tool shared by the timeline clip lanes and the piano roll.
 *
 * One active tool is owned by TimelinePanelComponent and pushed into both editors (clip lane and
 * piano roll swap through the same rect, so a single strip controls whichever is visible). The
 * enum is deliberately free of any JUCE dependency: gesture routing in the editors and the strip's
 * button wiring both switch on it, and the number-key mapping below is what the panel's
 * keyPressed() consults, so tests can drive tool switching without any UI.
 *
 * Numbering follows Cubase's tool row so the muscle memory transfers: 1 Select, 3 Split, 4 Glue,
 * 5 Erase, 7 Mute, 8 Draw. The gaps are reserved on purpose — 2 (Range Selection), 6 (Zoom) and
 * 9 (Play/Scrub) are Cubase tools we don't ship yet; keeping their digits free means adding one
 * later never reshuffles the keys users already learned.
 */
enum class EditTool {
    Select,
    Split,
    Glue,
    Erase,
    Mute,
    Draw,
};

inline constexpr std::array<EditTool, 6> kAllEditTools{
    EditTool::Select, EditTool::Split, EditTool::Glue, EditTool::Erase, EditTool::Mute, EditTool::Draw,
};

/** The Cubase-style number key that selects the tool (see the numbering note on EditTool). */
constexpr int editToolKeyDigit(EditTool tool) noexcept {
    switch (tool) {
    case EditTool::Select:
        return 1;
    case EditTool::Split:
        return 3;
    case EditTool::Glue:
        return 4;
    case EditTool::Erase:
        return 5;
    case EditTool::Mute:
        return 7;
    case EditTool::Draw:
        return 8;
    }
    return 1;
}

/** Display name used by the tool strip's tooltips and any menu text. */
constexpr const char* editToolName(EditTool tool) noexcept {
    switch (tool) {
    case EditTool::Select:
        return "Select";
    case EditTool::Split:
        return "Split";
    case EditTool::Glue:
        return "Glue";
    case EditTool::Erase:
        return "Erase";
    case EditTool::Mute:
        return "Mute";
    case EditTool::Draw:
        return "Draw";
    }
    return "Select";
}

/** Maps a pressed digit character ('1'..'9') to a tool, or nullopt for unassigned/reserved
 *  digits — callers must leave those keys unconsumed so they keep their meaning elsewhere. */
inline std::optional<EditTool> editToolForKeyChar(int keyChar) noexcept {
    for (auto tool : kAllEditTools)
        if (keyChar == '0' + editToolKeyDigit(tool))
            return tool;
    return std::nullopt;
}

} // namespace synth::ui
