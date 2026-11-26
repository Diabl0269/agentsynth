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
    /** The view the main area shows now: the stored one, or the Canvas while the stored one is suspended. */
    DockView getMainView() const noexcept { return suspended_ ? DockView::Canvas : mainView_; }
    bool isInMain(DockView view) const noexcept { return view == getMainView(); }
    /** The view the user chose for the main area, the one that is saved. */
    DockView getStoredMainView() const noexcept { return mainView_; }
    /** While the stored main view cannot fill the main area (it is in its own window, or its placement moved it
     *  out of the dock) the Canvas fills it for the session; the stored view is untouched and returns when the
     *  suspension ends. A new setMainView() or load() ends it. */
    bool isMainViewSuspended() const noexcept { return suspended_; }
    void setMainViewSuspended(bool suspended) noexcept { suspended_ = suspended && mainView_ != DockView::Canvas; }

    /** Puts `view` in the main area; the view that was there takes `view`'s slot in the order.
     *  False (nothing changes) when `view` is the stored main view. */
    bool setMainView(DockView view);
    /** Exchanges two views' slots; a no-op when either is missing or they are equal. */
    void swapSlots(DockView a, DockView b);
    /** Writes `view` into slot `index`; the caller keeps the order a permutation. */
    void setSlot(size_t index, DockView view);

private:
    std::vector<DockView> order_ = defaultOrder();
    DockView mainView_ = DockView::Canvas;
    bool suspended_ = false;
};

} // namespace synth::ui
