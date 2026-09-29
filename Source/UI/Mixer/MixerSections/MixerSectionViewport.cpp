// Concern: MixerSectionViewport's list fitting and its painted scroll thumb and fade.
#include "MixerSectionViewport.h"

#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"

namespace synth::ui {

// juce::Viewport wants keyboard focus by default, which would make the frame a focus trap inside a
// column: MixerPanelComponent is the mixer's single focusable leaf. Scrolling is wheel-only
// (setScrollOnDragMode(never)) so a drag inside the list stays the list's own gesture, like the send
// list's row drag-reorder. With no scrollbar shown, a wheel over a list that already fits leaves the
// view where it is, so juce::Viewport hands the event on to the column and the panel's own horizontal
// scroll, same as before the sections existed.
MixerSectionViewport::MixerSectionViewport() {
    setWantsKeyboardFocus(false);
    setScrollBarsShown(false, false, true, false);
    setScrollOnDragMode(ScrollOnDragMode::never);
    setSingleStepSizes(16, 16);
}

void MixerSectionViewport::setList(juce::Component& list) {
    setViewedComponent(&list, false);
    fitList();
}

void MixerSectionViewport::setContentHeight(int contentHeight) {
    contentHeight_ = juce::jmax(0, contentHeight);
    fitList();
}

bool MixerSectionViewport::isScrollable() const noexcept { return contentHeight_ > getHeight(); }

// The list is never shorter than the frame, so a click on the empty space below the last row still
// lands on the list (the insert list's right-click "Add..." relies on that).
void MixerSectionViewport::fitList() {
    auto* list = getViewedComponent();
    if (list == nullptr)
        return;
    list->setSize(getWidth(), juce::jmax(contentHeight_, getHeight()));
    // A list that got shorter can leave the view past its end; setViewPosition clamps it back.
    setViewPosition(getViewPosition());
    repaint();
}

void MixerSectionViewport::resized() {
    juce::Viewport::resized();
    fitList();
}

void MixerSectionViewport::visibleAreaChanged(const juce::Rectangle<int>&) { repaint(); }

void MixerSectionViewport::paintOverChildren(juce::Graphics& g) {
    if (!isScrollable() || getHeight() <= 0)
        return;
    const auto* laf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const auto thumbColour = laf != nullptr ? laf->getTheme().colors.textDisabled : juce::Colour(0xff5C6470);
    const auto surface = laf != nullptr ? laf->getTheme().colors.surface : juce::Colour(0xff1B1F26);

    const float visible = (float)getHeight();
    const float total = (float)contentHeight_;
    const float thumbHeight = juce::jmax(8.0f, visible * visible / total);
    const float travel = visible - thumbHeight;
    const float maxScroll = total - visible;
    const float thumbY = maxScroll > 0.0f ? travel * (float)getViewPositionY() / maxScroll : 0.0f;
    g.setColour(thumbColour);
    g.fillRoundedRectangle((float)(getWidth() - kThumbWidth), thumbY, (float)kThumbWidth, thumbHeight, 1.5f);

    // The fade marks "more below"; it goes once the list is scrolled to its end.
    if (getViewPositionY() < (int)maxScroll) {
        const auto fade = getLocalBounds().removeFromBottom(kFadeHeight).toFloat();
        g.setGradientFill(
            juce::ColourGradient(surface.withAlpha(0.0f), 0.0f, fade.getY(), surface, 0.0f, fade.getBottom(), false));
        g.fillRect(fade.withTrimmedRight((float)kThumbWidth + 1.0f));
    }
}

} // namespace synth::ui
