// ModuleLibraryLayout.cpp -- juce::Component layout overrides (resized/lookAndFeelChanged/
// parentHierarchyChanged), the scroll offset and its juce::ScrollBar plumbing.
#include "ModuleLibraryComponent.h"

bool ModuleLibraryComponent::setScrollOffset(int newOffset) {
    const int clamped = juce::jlimit(0, getMaxScrollOffset(), newOffset);
    if (clamped == scrollOffset)
        return false;
    scrollOffset = clamped;
    repaint();
    return true;
}

void ModuleLibraryComponent::resized() {
    searchEditor.setBounds(8, 4, juce::jmax(0, getWidth() - 16), kSearchHeight - 8);
    updateScrollBar();
    reclampFloatingHelpPopover();
}

void ModuleLibraryComponent::lookAndFeelChanged() {
    // Scrollbar width is a theme token (AppLookAndFeel::kScrollbarWidth), so a theme switch can
    // change the bar's footprint and the width left for row text.
    applySearchEditorColours();
    updateScrollBar();
}

void ModuleLibraryComponent::parentHierarchyChanged() {
    // getLookAndFeel() is only guaranteed to reflect the FINAL (persisted) theme once this
    // component is actually parented — MainComponent applies the persisted theme in its ctor
    // BODY, after this component (a plain member) was already default-constructed against
    // whatever theme was active at that earlier point (see MainComponent.cpp's ordering
    // contract). Re-pull colours the moment we're parented, which always happens after that
    // point. If a future change parents this component before the persisted-theme applyTheme()
    // call, this fix would need to move too.
    applySearchEditorColours();
}

void ModuleLibraryComponent::scrollBarMoved(juce::ScrollBar* bar, double newRangeStart) {
    if (bar == &verticalScrollBar)
        setScrollOffset(juce::roundToInt(newRangeStart));
}

int ModuleLibraryComponent::getRowContentWidth() const {
    return getWidth() - (verticalScrollBar.isVisible() ? verticalScrollBar.getWidth() : 0);
}

void ModuleLibraryComponent::updateScrollBar() {
    const int viewportHeight = getHeight() - kPinnedChromeHeight;
    const int scrollableHeight = getTotalContentHeight() - kPinnedChromeHeight;
    const bool needed = viewportHeight > 0 && scrollableHeight > viewportHeight;

    verticalScrollBar.setVisible(needed);
    setScrollOffset(scrollOffset); // re-clamp against the new maximum (0 when it no longer fits)

    if (!needed)
        return;

    const int barWidth = getScrollBarWidth();
    verticalScrollBar.setBounds(getWidth() - barWidth, kPinnedChromeHeight, barWidth, viewportHeight);
    verticalScrollBar.setSingleStepSize((double)kItemHeight);
    verticalScrollBar.setRangeLimits(0.0, (double)scrollableHeight, juce::dontSendNotification);
    verticalScrollBar.setCurrentRange((double)scrollOffset, (double)viewportHeight, juce::dontSendNotification);
}
