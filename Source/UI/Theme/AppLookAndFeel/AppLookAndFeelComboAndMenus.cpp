#include "AppLookAndFeel.h"

namespace synth::theme {

// Concern: combo boxes and popup menus.

void AppLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown, int /*buttonX*/,
                                  int /*buttonY*/, int /*buttonW*/, int /*buttonH*/, juce::ComboBox& box) {
    const auto& c = theme.colors;
    const auto& m = theme.metrics;
    const bool enabled = box.isEnabled();

    auto bounds = juce::Rectangle<float>(0, 0, (float)width, (float)height).reduced(0.5f);

    // Fill keyed by state: pressed → raised surface; disabled → dimmed surface; else surface.
    juce::Colour fill = c.surface;
    if (!enabled)
        fill = c.surface.withAlpha(0.5f);
    else if (isButtonDown)
        fill = c.surfaceHi;
    g.setColour(fill);
    g.fillRoundedRectangle(bounds, m.pillRadius);

    // Outline: accent when focused, else themed border.
    g.setColour(box.hasKeyboardFocus(false) ? c.accent : box.findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(bounds, m.pillRadius, m.borderWidth);

    // Chevron: a 2-segment "v" path stroked in the muted arrow colour (dimmed when disabled).
    const float cx = (float)width - 14.0f;
    const float cy = (float)height * 0.5f;
    juce::Path chevron;
    chevron.startNewSubPath(cx - kComboArrowSize, cy - kComboArrowSize * 0.4f);
    chevron.lineTo(cx, cy + kComboArrowSize * 0.6f);
    chevron.lineTo(cx + kComboArrowSize, cy - kComboArrowSize * 0.4f);

    auto arrowCol = box.findColour(juce::ComboBox::arrowColourId);
    g.setColour(enabled ? arrowCol : arrowCol.withMultipliedAlpha(0.4f));
    g.strokePath(chevron, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Waveform icon: if the selected item carries a Drawable icon, render it inside the closed
    // combo box to the left of the text label (which positionComboBoxText positions at x=8).
    // We iterate the root menu to find the item matching the current selection.
    const int selectedId = box.getSelectedId();
    if (selectedId > 0) {
        const juce::PopupMenu* rootMenu = box.getRootMenu();
        if (rootMenu != nullptr) {
            juce::PopupMenu::MenuItemIterator it(*rootMenu, false);
            while (it.next()) {
                const auto& item = it.getItem();
                if (item.itemID == selectedId && item.image != nullptr) {
                    constexpr int kIconSize = 14;
                    // Place the icon at x=6, vertically centred. positionComboBoxText shifts
                    // the text label right to x=24 (6 + 14 px icon + 4 px gap) when an icon is present.
                    const int iconX = 6;
                    const int iconY = (height - kIconSize) / 2;
                    auto iconBounds =
                        juce::Rectangle<float>((float)iconX, (float)iconY, (float)kIconSize, (float)kIconSize);
                    g.saveState();
                    g.reduceClipRegion(iconBounds.toNearestInt());
                    item.image->drawWithin(g, iconBounds, juce::RectanglePlacement::centred, 1.0f);
                    g.restoreState();
                    break;
                }
            }
        }
    }
}

void AppLookAndFeel::drawComboBoxTextWhenNothingSelected(juce::Graphics& g, juce::ComboBox& box, juce::Label& label) {
    g.setColour(box.findColour(juce::ComboBox::textColourId).withMultipliedAlpha(0.4f));
    g.setFont(label.getFont());
    g.drawFittedText(box.getTextWhenNothingSelected(), label.getBounds(), label.getJustificationType(), 1);
}

void AppLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
    // If the selected item carries a Drawable icon, shift the text label right to leave room
    // for the ~14 px icon (painted in drawComboBox) plus a 4 px gap.
    int leftOffset = 8;
    const int selectedId = box.getSelectedId();
    if (selectedId > 0) {
        const juce::PopupMenu* rootMenu = box.getRootMenu();
        if (rootMenu != nullptr) {
            juce::PopupMenu::MenuItemIterator it(*rootMenu, false);
            while (it.next()) {
                const auto& item = it.getItem();
                if (item.itemID == selectedId) {
                    if (item.image != nullptr)
                        leftOffset = 6 + 14 + 4; // iconX + kIconSize + gap
                    break;
                }
            }
        }
    }
    label.setBounds(leftOffset, 1, box.getWidth() - leftOffset - 22, box.getHeight() - 2);
    label.setFont(juce::Font(juce::FontOptions(theme.type.label + 2.0f)));
}

void AppLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height) {
    const auto& c = theme.colors;
    const auto& m = theme.metrics;
    auto bounds = juce::Rectangle<float>(0, 0, (float)width, (float)height);
    g.setColour(c.surface);
    g.fillRoundedRectangle(bounds, m.cornerRadius * 0.6f);
    g.setColour(c.border);
    g.drawRoundedRectangle(bounds.reduced(0.5f), m.cornerRadius * 0.6f, m.borderWidth);
}

void AppLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator,
                                       bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                                       const juce::String& text, const juce::String& shortcutKeyText,
                                       const juce::Drawable* icon, const juce::Colour* textColour) {
    const auto& c = theme.colors;
    const auto& m = theme.metrics;

    if (isSeparator) {
        g.setColour(c.border);
        g.fillRect(area.reduced(6, area.getHeight() / 2).withHeight(1));
        return;
    }

    // Highlight only a live (active) item under the cursor.
    if (isHighlighted && isActive) {
        g.setColour(c.accent.withAlpha(0.22f));
        g.fillRoundedRectangle(area.toFloat().reduced(2.0f, 1.0f), m.cornerRadius * 0.6f);
    }

    // Text colour: explicit override > active primary > disabled dim.
    const juce::Colour col = textColour != nullptr ? *textColour : (isActive ? c.textPrimary : c.textDisabled);
    g.setColour(col);
    g.setFont(juce::Font(juce::FontOptions(theme.type.label + 2.5f)));

    auto textArea = area.reduced(10, 0);

    // Drawn checkmark (no Unicode glyph — avoids font-dependent rendering).
    // Skipped when the item has an icon: the glyph + the closed-combo selection already
    // communicate the choice, and drawing both causes a visual overlap.
    if (isTicked && icon == nullptr) {
        const auto tickArea =
            juce::Rectangle<float>((float)area.getX() + 4.0f, (float)area.getY(), 18.0f, (float)area.getHeight());
        juce::Path tick;
        tick.startNewSubPath(tickArea.getX() + tickArea.getWidth() * 0.2f, tickArea.getCentreY());
        tick.lineTo(tickArea.getX() + tickArea.getWidth() * 0.42f,
                    tickArea.getCentreY() + tickArea.getHeight() * 0.18f);
        tick.lineTo(tickArea.getX() + tickArea.getWidth() * 0.78f,
                    tickArea.getCentreY() - tickArea.getHeight() * 0.22f);
        g.setColour(c.accent);
        g.strokePath(tick, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(col);

        // Shift text right so it does not overlap the checkmark tick
        textArea = textArea.withTrimmedLeft(16);
    }

    // Waveform glyph icon: paint a ~14x14 Drawable to the left of the text.
    if (icon != nullptr) {
        constexpr int kIconSize = 14;
        const int iconX = textArea.getX();
        const int iconY = area.getY() + (area.getHeight() - kIconSize) / 2;
        auto iconBounds = juce::Rectangle<float>((float)iconX, (float)iconY, (float)kIconSize, (float)kIconSize);
        g.saveState();
        g.reduceClipRegion(iconBounds.toNearestInt());
        icon->drawWithin(g, iconBounds, juce::RectanglePlacement::centred, 1.0f);
        g.restoreState();
        // Shift text right so it doesn't overlap the icon (icon width + 4px gap).
        textArea = textArea.withTrimmedLeft(kIconSize + 4);
    }

    g.drawText(text, textArea, juce::Justification::centredLeft, true);

    // Submenu arrow takes priority over the shortcut readout on the right edge.
    if (hasSubMenu) {
        const float ax = (float)area.getRight() - 12.0f;
        const float ay = (float)area.getCentreY();
        const float h = 4.0f;
        juce::Path arrow;
        arrow.startNewSubPath(ax - 2.0f, ay - h);
        arrow.lineTo(ax + 2.0f, ay);
        arrow.lineTo(ax - 2.0f, ay + h);
        g.setColour(c.textMuted);
        g.strokePath(arrow, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    } else if (shortcutKeyText.isNotEmpty()) {
        g.setColour(c.textMuted);
        g.drawText(shortcutKeyText, textArea, juce::Justification::centredRight, true);
    }
}

} // namespace synth::theme
