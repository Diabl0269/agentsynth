#pragma once

#include "EditTool.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/**
 * @brief Builds the custom mouse cursor shown while a timeline edit tool (Select / Split / Glue /
 * Erase / Mute / Draw — see EditTool.h) is active over the clip lanes or piano roll.
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
 * cursor's "hot" pixel floating in empty space to one side of the visible glyph. Split / Glue /
 * Erase / Mute have no such single working point (a scissors' cut happens where the blades cross,
 * which IS the icon's centre; glue/erase/mute act on whatever is directly under the pointer), so
 * those four use the icon's centre unmodified.
 *
 * Headless-safe by construction: `IconLibrary::getDrawable` (via `AppLookAndFeel::getIcon`)
 * returns nullptr when the asset library isn't linked in (headless test builds have no
 * HAS_FONT_ASSETS). This function never dereferences a null icon — it falls back to a stock
 * juce::MouseCursor instead, chosen per tool so the fallback still telegraphs SOME meaning:
 * Select keeps the platform's ordinary arrow, Draw gets a crosshair (the closest stock cursor to
 * "drawing at a point"), and the remaining four tools — which have no single obvious stock
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

    switch (tool) {
    case EditTool::Select:
        return juce::MouseCursor(image, 4, 2); // arrow tip
    case EditTool::Draw:
        return juce::MouseCursor(image, 3, 21); // pencil tip
    case EditTool::Split:
    case EditTool::Glue:
    case EditTool::Erase:
    case EditTool::Mute:
        return juce::MouseCursor(image, 12, 12); // icon centre
    }
    return juce::MouseCursor(image, 12, 12);
}

} // namespace synth::ui
