#include "UI/Settings/SoundPacks/SoundPackDisclosure.h"

#include "UI/Layout/FocusRing.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

namespace {
constexpr float kChevronSize = 8.0f;
}

SoundPackDisclosure::SoundPackDisclosure()
    : juce::Button("Details") {
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    addShortcut(juce::KeyPress(juce::KeyPress::returnKey)); // Space already clicks a focused button
    refreshText();
    onClick = [this] {
        if (onOpenRequested)
            onOpenRequested(!open_);
    };
}

void SoundPackDisclosure::setPackName(const juce::String& name) {
    name_ = name;
    refreshText();
}

void SoundPackDisclosure::setOpen(bool open, bool animate) {
    open_ = open;
    refreshText();
    if (animate)
        turn_.setOpen(open);
    else
        turn_.snapTo(open);
}

// The accessible name carries the state, so a screen reader hears it change.
void SoundPackDisclosure::refreshText() {
    setTitle("Details for " + name_ + (open_ ? ", expanded" : ", collapsed"));
    setTooltip(open_ ? "Hide the details of " + name_
                     : "Show who made " + name_ + ", its licence and every instrument");
}

void SoundPackDisclosure::paintButton(juce::Graphics& g, bool hot, bool /*down*/) {
    const auto bounds = getLocalBounds().toFloat();
    const bool lit = hot || hasKeyboardFocus(false);
    synth::theme::paintDisclosureChevron(g, bounds.withSizeKeepingCentre(kChevronSize, kChevronSize), turn_.openness(),
                                         synth::theme::themeOf(*this), lit);
    paintFocusRing(g, bounds.reduced(1.0f), *this);
}

} // namespace synth::ui
