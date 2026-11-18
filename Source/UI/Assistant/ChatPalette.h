#pragma once

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/ReadableText.h"
#include "UI/Theme/Theme.h"

namespace synth::ui {

// Every colour the AI chat panel draws that is not already a plain theme token, resolved from the theme's tokens so
// the panel re-skins with the app and stays legible on each built-in canvas (Daylight included). Text roles are
// nudged to the readable contrast against the surfaces they sit on; the ChatPalette tests check that on every built-in
// theme.
struct ChatPalette {
    juce::Colour upgradeFill, upgradeText; // the purple "Upgrade to Pro" button
    juce::Colour cancelFill, cancelText;   // the Cancel button while a request runs
    juce::Colour debugBackground, debugText;
    juce::Colour userBubble, assistantBubble; // bubble tints (drawn at low alpha over the panel)
    juce::Colour userRole;                    // "YOU" caption and the Pro plan badge
    juce::Colour mutedText;                   // AI caption, timestamp, waiting line, placeholder, status lines
    juce::Colour panelFill;                   // the panel's own fill
};

inline ChatPalette makeChatPalette(const synth::theme::Colors& c) {
    using synth::theme::nudgeToReadable;
    using synth::theme::readableTextOn;
    const std::vector<juce::Colour> panelSurfaces{c.bg0, c.bg1, c.surface};
    ChatPalette p;
    p.upgradeFill = c.hueViolet;
    p.upgradeText = readableTextOn(p.upgradeFill, c.iconPaper);
    p.cancelFill = c.error;
    p.cancelText = readableTextOn(p.cancelFill, c.iconPaper);
    p.debugBackground = c.bg0;
    p.debugText = nudgeToReadable(c.success, {c.bg0});
    p.userBubble = c.accent;
    p.assistantBubble = c.surfaceHi;
    p.userRole = nudgeToReadable(c.accent, panelSurfaces);
    p.mutedText = nudgeToReadable(c.textMuted, panelSurfaces);
    p.panelFill = c.bg0;
    return p;
}

// The palette of `component`'s look and feel; a component outside the app's themed look and feel (a bare test
// harness) gets the default (Obsidian) tokens.
inline ChatPalette chatPaletteFor(const juce::Component& component) {
    const auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&component.getLookAndFeel());
    return makeChatPalette(lf != nullptr ? lf->getTheme().colors : synth::theme::Colors{});
}

} // namespace synth::ui
