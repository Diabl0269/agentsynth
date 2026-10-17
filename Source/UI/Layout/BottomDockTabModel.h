#pragma once

#include <juce_core/juce_core.h>
#include <optional>
#include <vector>

// BottomDockTabModel.h (docs/mixer/panel.md#the-tab-strip): which of the four views sits in the main
// area and in what order the others are offered as bottom-panel tabs. Pure data, no components, so
// the order parse/migration, the swap and the offered-tab rule are testable headlessly.
namespace synth::ui {

/** The four views the app can show: one sits in the main area, the rest are bottom-panel tabs. */
enum class DockView { Timeline, Mixer, MidiRemote, Canvas };

class BottomDockTabModel {
public:
    /** False until the main area can host a view other than the canvas: a stored non-canvas main
     *  view is kept and written back, but the effective main view stays the canvas. */
    static constexpr bool kMainViewSwapEnabled = false;

    /** The settings vocabulary ("timeline", "mixer", "midiRemote", "canvas"), shared by every key. */
    static const char* keyFor(DockView view) noexcept;
    static std::optional<DockView> viewForKey(const juce::String& key);

    /** {Timeline, Mixer, MidiRemote, Canvas}. */
    static std::vector<DockView> defaultOrder();
    /** A saved comma-joined order: 3 distinct non-canvas names (Canvas appended) or all 4 distinct
     *  names; nullopt for anything else. */
    static std::optional<std::vector<DockView>> parseOrder(const juce::String& saved);
    static juce::String serializeOrder(const std::vector<DockView>& order);

    /** Replaces the order and main view from saved strings; malformed values fall back to the
     *  defaults (the order to defaultOrder(), the main view to Canvas). */
    void load(const juce::String& savedOrder, const juce::String& savedMainView);

    /** Always a permutation of all four views; the main view keeps its (hidden) slot. */
    const std::vector<DockView>& getOrder() const noexcept { return order_; }
    /** The view the main area shows. */
    DockView getMainView() const noexcept { return mainView_; }
    /** The value to persist: equals getMainView() except after loading a clamped value. */
    DockView getStoredMainView() const noexcept { return storedMainView_; }
    bool isInMain(DockView view) const noexcept { return view == mainView_; }

    /** Puts `view` in the main area; the view that was there takes `view`'s slot in the order.
     *  False (nothing changes) when `view` is already in main. */
    bool setMainView(DockView view);
    /** Exchanges two views' slots; a no-op when either is missing or they are equal. */
    void swapSlots(DockView a, DockView b);
    /** Writes `view` into slot `index`; the caller keeps the order a permutation. */
    void setSlot(size_t index, DockView view);

private:
    std::vector<DockView> order_ = defaultOrder();
    DockView mainView_ = DockView::Canvas;
    DockView storedMainView_ = DockView::Canvas;
};

} // namespace synth::ui
