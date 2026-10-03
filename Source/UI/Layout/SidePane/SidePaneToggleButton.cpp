// Concern: SidePaneToggleButton's state mirroring, tooltip and sidebar glyph.
#include "SidePaneToggleButton.h"

namespace synth::ui {

SidePaneToggleButton::SidePaneToggleButton()
    : IconButton("Side pane", synth::theme::Glyph::SidePane, Style::Bare) {
    setGlyphWhenOn(synth::theme::Glyph::SidePaneOpen);
    setClickingTogglesState(false); // the pane is the source of truth, see refresh()
    setWantsKeyboardFocus(true);
    setTitle("Side pane");
    setDescription("Shows or hides this tab's side pane");
    onClick = [this] {
        if (pane_ != nullptr)
            pane_->toggle();
    };
    refresh();
}

SidePaneToggleButton::~SidePaneToggleButton() { bind(nullptr); }

void SidePaneToggleButton::bind(SidePane* pane) {
    if (pane_ == pane)
        return;
    if (pane_ != nullptr)
        pane_->removeChangeListener(this);
    pane_ = pane;
    if (pane_ != nullptr)
        pane_->addChangeListener(this);
    refresh();
}

void SidePaneToggleButton::setShortcutText(const juce::String& text) {
    shortcutText_ = text;
    refresh();
}

void SidePaneToggleButton::refresh() {
    const bool has = pane_ != nullptr && pane_->hasContent();
    const bool open = has && pane_->isOpen();
    setVisible(has);
    setToggleState(open, juce::dontSendNotification);
    setTooltip(formatShortcutHint(open ? "Hide side pane" : "Show side pane", shortcutText_));
    repaint();
}

} // namespace synth::ui
