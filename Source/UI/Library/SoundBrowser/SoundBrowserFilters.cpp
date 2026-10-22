// SoundBrowserFilters.cpp -- the Filters button, the chip row and the panel under the search: how they stack above the
// list (the list is pushed down, never covered), the 190 ms grow of the panel, and how a change reaches the list.
#include "SoundBrowser/SoundSearch.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"

namespace synth::ui {

namespace {
constexpr int kMinListHeight = 96; // the panel never takes the last of the list
}

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
    panelHost_.addAndMakeVisible(filterPanel_);
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

int SoundBrowserComponent::panelFullHeight() const {
    const int natural = filterPanel_.heightForWidth(getWidth());
    const int chips = juce::roundToInt((float)SoundFilterChipRow::kHeight * chipRow_.rowPresence());
    const int room = getHeight() - kSearchHeight - chips - kPathHeight - kFooterHeight - kMinListHeight;
    return juce::jmax(0, juce::jmin(natural, room));
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

// Stacks the rows above the list: the search and Filters button, the chip row (which grows with its chips), the panel
// (which grows open) and the path bar. The list starts under all of them, so opening the panel moves it down.
void SoundBrowserComponent::layoutTop() {
    const int w = getWidth();
    const int buttonWidth = kFiltersButtonWidth;
    search_.setBounds(8, 4, juce::jmax(0, w - 16 - buttonWidth - 6), kSearchHeight - 8);
    filtersButton_.setBounds(w - 8 - buttonWidth, 4, buttonWidth, kSearchHeight - 8);

    const int chipHeight = juce::roundToInt((float)SoundFilterChipRow::kHeight * chipRow_.rowPresence());
    chipRow_.setBounds(0, kSearchHeight, w, chipHeight);
    chipRow_.setVisible(chipHeight > 0);

    const int full = panelFullHeight();
    const float p = panelSlide_.getProgress();
    const bool fade = prefersReducedMotion();
    const int panelHeight = fade ? (p > 0.0f ? full : 0) : juce::roundToInt(p * (float)full);
    panelHost_.setBounds(0, kSearchHeight + chipHeight, w, panelHeight);
    panelHost_.setAlpha(fade ? p : 1.0f);
    panelHost_.setVisible(panelHeight > 0);
    filterPanel_.setBounds(0, 0, w, juce::jmax(full, 1));

    listTop_ = kSearchHeight + chipHeight + panelHeight + kPathHeight;
    layoutPathParts();
    updateScrollBar();
    repaint();
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
        filtersButton_.grabKeyboardFocus(); // the panel's controls are about to go
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
