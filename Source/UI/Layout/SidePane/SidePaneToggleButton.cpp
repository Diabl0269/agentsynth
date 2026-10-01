// Concern: SidePaneToggleButton's state mirroring, tooltip and sidebar glyph.
#include "SidePaneToggleButton.h"

#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

SidePaneToggleButton::SidePaneToggleButton()
    : juce::Button("Side pane") {
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

// A window glyph whose left strip is filled while the pane is open, inside the same accent wash the
// other toolbar toggles use.
void SidePaneToggleButton::paintButton(juce::Graphics& g, bool highlighted, bool down) {
    const auto* laf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const auto accent = laf != nullptr ? laf->getTheme().colors.accent : juce::Colour(0xff00D1FF);
    const auto text = laf != nullptr ? laf->getTheme().colors.textPrimary : juce::Colour(0xffEAEEF3);

    auto bounds = getLocalBounds().toFloat().reduced(1.0f, 2.0f);
    if (getToggleState()) {
        g.setColour(accent.withAlpha(0.18f));
        g.fillRoundedRectangle(bounds, 3.0f);
    } else if (highlighted || down) {
        g.setColour(text.withAlpha(0.08f));
        g.fillRoundedRectangle(bounds, 3.0f);
    }

    const auto glyph = juce::Rectangle<float>(14.0f, 11.0f).withCentre(bounds.getCentre());
    const auto colour = getToggleState() ? accent : text.withAlpha(0.75f);
    g.setColour(colour);
    g.drawRoundedRectangle(glyph, 1.5f, 1.2f);
    const auto strip = glyph.withWidth(5.0f);
    if (getToggleState())
        g.fillRect(strip.reduced(0.6f));
    else
        g.fillRect(strip.getRight() - 0.6f, glyph.getY() + 1.0f, 1.2f, glyph.getHeight() - 2.0f);

    paintFocusRing(g, getLocalBounds().toFloat(), *this, 3.0f);
}

} // namespace synth::ui
