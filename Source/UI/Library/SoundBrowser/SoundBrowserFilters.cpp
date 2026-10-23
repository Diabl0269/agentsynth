// SoundBrowserFilters.cpp -- the Filters button, the chip row and the panel under the search: how they stack above the
// list (the list is pushed down, never covered), the 190 ms grow of the panel, and how a change reaches the list.
#include "SoundBrowser/SoundSearch.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"

namespace synth::ui {

void SoundBrowserComponent::setupFilters() {
    filtersButton_.setTooltip("Narrow by kind, sound, tempo, key, length or place");
    filtersButton_.setClickingTogglesState(false);
    filtersButton_.onClick = [this] { setFiltersOpen(!filtersOpen_); };
    addAndMakeVisible(filtersButton_);

    chipRow_.onRemove = [this](const juce::String& id) {
        filters_.remove(id);
        applyFiltersChanged();
        if (filters_.count() == 0 && chipRow_.hasKeyboardFocus(false))
            filtersButton_.grabKeyboardFocus();
    };
    chipRow_.onClearAll = [this] {
        filters_.clearAll();
        applyFiltersChanged();
        filtersButton_.grabKeyboardFocus();
    };
    chipRow_.onMotionFrame = [this] { layoutTop(); };
    addChildComponent(chipRow_);

    filterPanel_.onChanged = [this] { filtersChanged(); };
    filterPanel_.onCloseRequested = [this] { setFiltersOpen(false); };
    panelHost_.setViewedComponent(&filterPanel_, false);
    panelHost_.setWantsKeyboardFocus(false); // the controls inside are the tab stops, not the scroller
    panelHost_.setScrollBarsShown(false, false);
    addChildComponent(panelHost_);
    refreshPlaces();
    refreshFilterButton();
}

synth::soundbrowser::SoundFilter SoundBrowserComponent::currentFilter() {
    filters_.setSongTempo(songTempoProvider ? songTempoProvider() : 120.0);
    return filters_.toFilter();
}

bool SoundBrowserComponent::showsFlatResults() const {
    return isSearchActive() || (placeId_.isEmpty() && filters_.count() > 0);
}

int SoundBrowserComponent::chipRowHeight() const {
    return juce::roundToInt((float)SoundFilterChipRow::kHeight * chipRow_.rowPresence());
}

int SoundBrowserComponent::footerHeight() const {
    switch (footerMode_) {
    case FooterMode::Full:
        return kFooterHeight;
    case FooterMode::Compact:
        return kCompactFooterHeight;
    case FooterMode::Hidden:
        break;
    }
    return 0;
}

// What gives way as the column shortens. Panel closed: the footer goes first (whole, one row, none) while the list
// keeps its minimum rows. Panel open: it is guaranteed its minimum height, and room for it comes from the footer
// (whole to one row to none), then the list (2 rows, 1, 0); only then does the panel shrink below its minimum.
// `heightAbove` is the search row, chips and path bar.
SoundBrowserComponent::Stack SoundBrowserComponent::chooseStack(int heightAbove) const {
    const int room = getHeight() - heightAbove;
    const int listMin = kMinListRows * kRowHeight;
    if (!(filtersOpen_ || panelSlide_.getProgress() > 0.0f)) {
        const int spare = room - listMin;
        if (spare >= kFooterHeight)
            return {FooterMode::Full, listMin};
        return {spare >= kCompactFooterHeight ? FooterMode::Compact : FooterMode::Hidden, listMin};
    }
    const int panelMin = juce::jmin(SoundFilterPanel::kMinVisibleHeight, filterPanel_.heightForWidth(getWidth()));
    const Stack order[] = {{FooterMode::Full, listMin},
                           {FooterMode::Compact, listMin},
                           {FooterMode::Hidden, listMin},
                           {FooterMode::Hidden, kRowHeight},
                           {FooterMode::Hidden, 0}};
    for (const auto& s : order) {
        const int footer = s.footer == FooterMode::Full      ? kFooterHeight
                           : s.footer == FooterMode::Compact ? kCompactFooterHeight
                                                             : 0;
        if (room - footer - s.listReserve >= panelMin)
            return s;
    }
    return {FooterMode::Hidden, 0};
}

// Puts the footer at the bottom in the shape the mode asks for; a hidden footer is invisible, so nothing in it is
// reachable by keyboard or screen reader.
void SoundBrowserComponent::layoutFooter() {
    footer_.setVisible(footerMode_ != FooterMode::Hidden);
    if (footerMode_ == FooterMode::Hidden)
        return;
    footer_.setCompact(footerMode_ == FooterMode::Compact);
    const int h = footerMode_ == FooterMode::Full ? SoundPreviewFooter::kHeight : kCompactFooterHeight;
    footer_.setBounds(0, getHeight() - h, getWidth(), h);
}

void SoundBrowserComponent::refreshFilterButton() {
    const int n = filters_.count();
    filtersButton_.setButtonText(n > 0 ? "Filters (" + juce::String(n) + ")" : juce::String("Filters"));
    filtersButton_.setTitle("Filters, " + juce::String(n) + " on, " + (filtersOpen_ ? "expanded" : "collapsed"));
    filtersButton_.setToggleState(filtersOpen_, juce::dontSendNotification);
}

void SoundBrowserComponent::refreshPlaces() {
    std::vector<std::pair<juce::String, juce::String>> places;
    const auto snap = index_.snapshot();
    for (const bool packs : {true, false})
        for (const auto& p : snap->places)
            if (p.isPack == packs)
                places.emplace_back(p.id, p.name);
    filterPanel_.setPlaces(std::move(places));
}

// The panel's open height is what it needs, capped to what the footer and the list's reserve leave. When
// it is capped it scrolls inside its own viewport (the scroll bar takes some of its width), so its controls are never
// clipped away. While it grows the viewport follows the animation and the content keeps its full size.
void SoundBrowserComponent::layoutPanel(int chipHeight) {
    const int w = getWidth();
    const int room = getHeight() - kSearchHeight - chipHeight - kPathHeight - footerHeight() - listReserve_;
    const int cap = juce::jmax(0, room);
    int contentWidth = w;
    int natural = filterPanel_.heightForWidth(contentWidth);
    const bool scrolls = natural > cap;
    if (scrolls) {
        contentWidth = juce::jmax(0, w - panelHost_.getScrollBarThickness());
        natural = filterPanel_.heightForWidth(contentWidth);
    }
    const int full = juce::jmin(natural, cap);
    const float p = panelSlide_.getProgress();
    const bool fade = prefersReducedMotion();
    const int panelHeight = fade ? (p > 0.0f ? full : 0) : juce::roundToInt(p * (float)full);
    panelHost_.setScrollBarsShown(scrolls, false);
    panelHost_.setBounds(0, kSearchHeight + chipHeight, w, panelHeight);
    panelHost_.setAlpha(fade ? p : 1.0f);
    panelHost_.setVisible(panelHeight > 0);
    filterPanel_.setBounds(0, 0, contentWidth, juce::jmax(natural, 1));
    listTop_ = kSearchHeight + chipHeight + panelHeight + kPathHeight;
}

// Stacks the rows above the list: the search and Filters button, the chip row (which grows with its chips), the panel
// (which grows open) and the path bar. The list starts under all of them, so opening the panel moves it down; the
// footer is chosen first because the panel and the list both give way to it.
void SoundBrowserComponent::layoutTop() {
    const int w = getWidth();
    const int buttonWidth = kFiltersButtonWidth;
    search_.setBounds(8, 4, juce::jmax(0, w - 16 - buttonWidth - 6), kSearchHeight - 8);
    filtersButton_.setBounds(w - 8 - buttonWidth, 4, buttonWidth, kSearchHeight - 8);

    const int chipHeight = chipRowHeight();
    chipRow_.setBounds(0, kSearchHeight, w, chipHeight);
    chipRow_.setVisible(chipHeight > 0);

    const auto stack = chooseStack(kSearchHeight + chipHeight + kPathHeight);
    footerMode_ = stack.footer;
    listReserve_ = stack.listReserve;
    layoutFooter();
    layoutPanel(chipHeight);
    layoutPathParts();
    updateScrollBar();
    attachReachListeners();
    repaint();
}

// A control inside a capped, scrolling panel that takes focus (Tab) scrolls into view; a focus change is also how the
// app learns it came back to the front.
void SoundBrowserComponent::globalFocusChanged(juce::Component* focused) {
    scrollFocusedControlIntoView(focused);
    if (isShowing())
        noteAppFocus();
}

void SoundBrowserComponent::scrollFocusedControlIntoView(juce::Component* focused) {
    if (focused == nullptr || !filterPanel_.isParentOf(focused))
        return;
    const auto box = filterPanel_.getLocalArea(focused, focused->getLocalBounds());
    const int viewTop = panelHost_.getViewPositionY();
    const int visible = panelHost_.getMaximumVisibleHeight();
    if (box.getY() < viewTop)
        panelHost_.setViewPosition(0, box.getY());
    else if (box.getBottom() > viewTop + visible)
        panelHost_.setViewPosition(0, box.getBottom() - visible);
}

void SoundBrowserComponent::setFiltersOpen(bool open, bool animate) {
    if (open == filtersOpen_)
        return;
    filtersOpen_ = open;
    if (open) {
        refreshPlaces();
        filterPanel_.syncFromModel();
    } else if (auto* focused = juce::Component::getCurrentlyFocusedComponent();
               focused != nullptr && (focused == &filterPanel_ || filterPanel_.isParentOf(focused))) {
        focusStop(filtersButton_); // the panel's controls are about to go
    }
    refreshFilterButton();

    const bool canAnimate = animate && (isShowing() || animateOffscreenForTest_);
    if (!panelSlide_.retarget(open ? 1.0f : 0.0f, canAnimate)) {
        if (vblank_.has_value())
            panelAnim_.stop(*vblank_);
        layoutTop();
        return;
    }
    if (!vblank_.has_value())
        vblank_.emplace(this);
    layoutTop();
    panelAnim_.start(
        *vblank_, motionMs(190.0, 80.0), easeOutCubic,
        [this](float t) {
            panelSlide_.applyTweenAt(t);
            layoutTop();
        },
        [this] {
            panelSlide_.finish();
            layoutTop();
        });
}

// A filter changed: the chips, the button and the list follow; the list fades like any other change of results.
void SoundBrowserComponent::filtersChanged() {
    chipRow_.setChips(filters_.chips());
    refreshFilterButton();
    capturePreviousList();
    scrollOffset_ = 0;
    rebuildRows();
    layoutTop();
    playListMotion(0);
}

void SoundBrowserComponent::applyFiltersChanged() {
    filterPanel_.syncFromModel();
    filtersChanged();
}

void SoundBrowserComponent::setSongKey(const juce::String& key) {
    filters_.setSongKey(key);
    applyFiltersChanged();
}

void SoundBrowserComponent::completeFilterMotionForTest() {
    panelAnim_.completeForTest();
    chipRow_.finishMotionForTest();
    layoutTop();
}

void SoundBrowserComponent::setAnimateOffscreenForTest(bool on) {
    animateOffscreenForTest_ = on;
    chipRow_.setAnimateOffscreenForTest(on);
}

} // namespace synth::ui
