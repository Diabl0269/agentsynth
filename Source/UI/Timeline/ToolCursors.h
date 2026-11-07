#pragma once

#include "EditTool.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace synth::ui {

/** The picture behind one custom tool cursor: a juce::MouseCursor cannot be read back, so test mode's screen frame
 *  (docs/development/test-mode.md) draws the pointer from the image `makeToolCursor` kept here. */
struct ToolCursorImage {
    juce::MouseCursor cursor;
    juce::String name; // "tool:range", "tool:draw" ...
    juce::Image image;
    juce::Point<int> hotspot;
};

/** The registered image of `cursor`, or null when it is a stock cursor or was not built by makeToolCursor. */
inline const ToolCursorImage* findToolCursorImage(const juce::MouseCursor& cursor);

namespace detail {
inline std::vector<ToolCursorImage>& toolCursorImages() {
    static std::vector<ToolCursorImage> images;
    return images;
}
inline const char* toolCursorName(EditTool tool) {
    switch (tool) {
    case EditTool::Select:
        return "tool:select";
    case EditTool::Range:
        return "tool:range";
    case EditTool::Split:
        return "tool:split";
    case EditTool::Glue:
        return "tool:glue";
    case EditTool::Erase:
        return "tool:erase";
    case EditTool::Mute:
        return "tool:mute";
    case EditTool::Draw:
        return "tool:draw";
    }
    return "tool";
}
} // namespace detail

inline const ToolCursorImage* findToolCursorImage(const juce::MouseCursor& cursor) {
    for (const auto& entry : detail::toolCursorImages())
        if (entry.cursor == cursor)
            return &entry;
    return nullptr;
}

/**
 * @brief Builds the custom mouse cursor shown while a timeline edit tool (Select / Range / Split /
 * Glue / Erase / Mute / Draw — see EditTool.h) is active over the clip lanes or piano roll.
 *
 * The cursor is rendered from the SAME already-tinted `Icon::Tool*` Drawable the tool strip's
 * button paints (see IconLibrary / AppLookAndFeel::retintIcons — the icon is tinted to
 * `textPrimary`, matching the other toolbar chrome), so the cursor never drifts out of sync with
 * whatever theme is active: no separate cursor-only asset or tint step exists to fall out of date.
 *
 * Hotspots are NOT all the same point, because a generic "centre of the icon" hotspot only reads
 * correctly for symmetric glyphs. Select and Draw are both icons whose silhouette has an obvious
 * working point away from centre (the arrow's tip, the pencil's point) — Cubase and every other
 * DAW places the click point there, and leaving it at the icon's geometric centre would show the
 * cursor's "hot" pixel floating in empty space to one side of the visible glyph. Range / Split /
 * Glue / Erase / Mute have no such single working point (a scissors' cut happens where the blades
 * cross, which IS the icon's centre; a range starts at, and glue/erase/mute act on, whatever is
 * directly under the pointer), so those five use the icon's centre unmodified.
 *
 * Headless-safe by construction: `IconLibrary::getDrawable` (via `AppLookAndFeel::getIcon`)
 * returns nullptr when the asset library isn't linked in (headless test builds have no
 * HAS_FONT_ASSETS). This function never dereferences a null icon — it falls back to a stock
 * juce::MouseCursor instead, chosen per tool so the fallback still telegraphs SOME meaning:
 * Select keeps the platform's ordinary arrow, Draw gets a crosshair (the closest stock cursor to
 * "drawing at a point"), Range gets the I-beam (a span select, like selecting text), and the
 * remaining four tools — which have no single obvious stock
 * equivalent — also get the crosshair rather than the ordinary arrow, so the user can at least
 * tell a non-Select tool is active.
 */
inline juce::MouseCursor makeToolCursor(EditTool tool, const juce::Drawable* icon) {
    if (icon == nullptr) {
        switch (tool) {
        case EditTool::Select:
            return juce::MouseCursor(juce::MouseCursor::NormalCursor);
        case EditTool::Draw:
            return juce::MouseCursor(juce::MouseCursor::CrosshairCursor);
        case EditTool::Range:
            return juce::MouseCursor(juce::MouseCursor::IBeamCursor); // a text-style span select
        case EditTool::Split:
        case EditTool::Glue:
        case EditTool::Erase:
        case EditTool::Mute:
            return juce::MouseCursor(juce::MouseCursor::CrosshairCursor);
        }
        return juce::MouseCursor(juce::MouseCursor::NormalCursor);
    }

    constexpr int kSize = 24;
    juce::Image image(juce::Image::ARGB, kSize, kSize, true); // transparent background
    juce::Graphics g(image);
    icon->drawWithin(g, juce::Rectangle<float>(0.0f, 0.0f, (float)kSize, (float)kSize),
                     juce::RectanglePlacement::centred, 1.0f);

    juce::Point<int> hotspot(12, 12); // icon centre
    if (tool == EditTool::Select)
        hotspot = {4, 2}; // arrow tip
    else if (tool == EditTool::Draw)
        hotspot = {3, 21}; // pencil tip
    const juce::MouseCursor cursor(image, hotspot.x, hotspot.y);
    // A theme change rebuilds the cursors, so the oldest pictures are dropped to keep the list small.
    auto& images = detail::toolCursorImages();
    constexpr std::size_t kMaxImages = 64;
    if (images.size() >= kMaxImages)
        images.erase(images.begin());
    images.push_back({cursor, detail::toolCursorName(tool), image, hotspot});
    return cursor;
}

} // namespace synth::ui
