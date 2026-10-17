// Concern: the bottom dock's view model -- the four-slot tab order, the main view, and how both are
// read from and written to the user settings.
#include "BottomDockTabModel.h"

#include <algorithm>

namespace synth::ui {

const char* BottomDockTabModel::keyFor(DockView view) noexcept {
    switch (view) {
    case DockView::Mixer:
        return "mixer";
    case DockView::MidiRemote:
        return "midiRemote";
    case DockView::Canvas:
        return "canvas";
    case DockView::Timeline:
        break;
    }
    return "timeline";
}

std::optional<DockView> BottomDockTabModel::viewForKey(const juce::String& key) {
    for (DockView v : defaultOrder())
        if (key == keyFor(v))
            return v;
    return std::nullopt;
}

// The canvas comes last so the three tabs an existing install already has keep their places, and a
// saved three-entry order migrates by appending it.
std::vector<DockView> BottomDockTabModel::defaultOrder() {
    return {DockView::Timeline, DockView::Mixer, DockView::MidiRemote, DockView::Canvas};
}

// A three-entry order is what builds before the canvas tab existed saved; it is accepted only
// without "canvas" in it and gains the canvas at the end. Anything else (wrong length, an unknown or
// repeated name) is rejected whole rather than half-applied. Builds before this one accept exactly
// three names, so the four-entry order written here makes them fall back to their own default.
std::optional<std::vector<DockView>> BottomDockTabModel::parseOrder(const juce::String& saved) {
    const auto names = juce::StringArray::fromTokens(saved, ",", "");
    if (names.size() != 3 && names.size() != 4)
        return std::nullopt;
    std::vector<DockView> parsed;
    for (const auto& name : names) {
        const auto view = viewForKey(name.trim());
        if (!view || std::find(parsed.begin(), parsed.end(), *view) != parsed.end())
            return std::nullopt;
        parsed.push_back(*view);
    }
    if (parsed.size() == 3) {
        if (std::find(parsed.begin(), parsed.end(), DockView::Canvas) != parsed.end())
            return std::nullopt;
        parsed.push_back(DockView::Canvas);
    }
    return parsed;
}

juce::String BottomDockTabModel::serializeOrder(const std::vector<DockView>& order) {
    juce::StringArray names;
    for (DockView v : order)
        names.add(keyFor(v));
    return names.joinIntoString(",");
}

// The stored main view always round-trips, so a value written by a build that can swap views
// survives a session in this one. Only the effective main view is clamped to the canvas while
// kMainViewSwapEnabled is false: nothing here can lay out the timeline, mixer or controllers in the
// main area yet, so honouring the value would hide that tab with nowhere left to show it.
void BottomDockTabModel::load(const juce::String& savedOrder, const juce::String& savedMainView) {
    order_ = parseOrder(savedOrder).value_or(defaultOrder());
    storedMainView_ = viewForKey(savedMainView).value_or(DockView::Canvas);
    mainView_ = kMainViewSwapEnabled ? storedMainView_ : DockView::Canvas;
}

// The order holds every view, the main one included, so exchanging the two slots is the whole swap:
// the view coming down appears exactly where the one going up was offered.
bool BottomDockTabModel::setMainView(DockView view) {
    if (view == mainView_)
        return false;
    swapSlots(mainView_, view);
    mainView_ = view;
    storedMainView_ = view;
    return true;
}

void BottomDockTabModel::swapSlots(DockView a, DockView b) {
    auto ia = std::find(order_.begin(), order_.end(), a);
    auto ib = std::find(order_.begin(), order_.end(), b);
    if (ia != order_.end() && ib != order_.end())
        std::iter_swap(ia, ib);
}

void BottomDockTabModel::setSlot(size_t index, DockView view) {
    if (index < order_.size())
        order_[index] = view;
}

} // namespace synth::ui
