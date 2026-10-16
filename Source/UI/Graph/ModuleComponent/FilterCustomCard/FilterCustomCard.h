#pragma once

// FilterCustomCard.h -- the Filter card's Custom-type section: an "Edit shapes" button, the morph map under the
// response view and the Show Morph Map pill. A collaborator of ModuleComponent, which hands it the few things it
// needs through `Host`.
#include "Modules/CardViewState.h"
#include "Modules/FilterModule.h"
#include "UI/Graph/CardBody/CardBlockFade.h"
#include "UI/ModuleViews/MorphMap/MorphMap.h"
#include "UI/ModuleViews/ShapeEditor/FilterShapesAdapter.h"
#include "UI/ModuleViews/ShapeEditor/ShapeMapController.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>

namespace synth::ui {

class FilterCustomCard {
public:
    struct Host {
        juce::Component* card = nullptr;
        std::function<void()> relayout;
        std::function<void()> makeRoom;
        bool* settling = nullptr;
        std::function<void(juce::PopupMenu&)> showMenu;
        std::function<void(const std::function<void(synth::CardViewState&)>&)> rememberView;
        std::function<void(bool)> hideMorphKnobs;
        std::function<juce::String()> title; ///< the card's title, for the window
    };

    /** The card, module and adapter must outlive this (detach() first). Message thread only for every member. */
    FilterCustomCard(Host host, FilterModule& module, std::shared_ptr<FilterShapesAdapter> adapter);
    ~FilterCustomCard();

    /** Re-reads the type, the shape count and the remembered choice and shows or hides the parts. */
    void sync();
    /** The card's gated tick: follows undo, the editor window, automation and the knobs. */
    void poll();
    /** Lays the parts out from `y` and returns the y below them. */
    int layout(int y, int x, int width, bool apply);
    /** Closes the editor window and drops everything that references the module. */
    void detach();

    void openEditor(int shapeIndex);
    /** The shape the cursor is closest to (the one that dominates the blend). */
    int nearestShapeToCursor() const;

    /** The map is on the card (shown or fading in): its sliders then stand in for the Morph X / Y knobs. */
    bool isMapShown() const;
    /** The raw CV channel of the slider under `cardPoint`, if the map is shown and a slider is there. */
    std::optional<int> channelAt(juce::Point<int> cardPoint) const;
    /** Where a cable for raw channel `channel` lands on the map's slider, card coordinates. */
    std::optional<juce::Point<float>> anchorFor(int channel) const;
    juce::Rectangle<int> stripBoundsFor(int channel) const;
    /** Lights the slider of `channel` as the pending drop target (-1 clears it). */
    void setDropChannel(int channel);

    MorphMap& getMap() noexcept { return map_; }
    juce::TextButton& getEditButton() noexcept { return editButton_; }
    juce::ToggleButton& getToggle() noexcept { return toggle_; }
    ShapeMapController& getController() noexcept { return *controller_; }
    FilterShapesAdapter& getAdapter() noexcept { return *adapter_; }

private:
    bool customActive() const;
    bool mapWanted() const;

    Host host_;
    FilterModule& module_;
    std::shared_ptr<FilterShapesAdapter> adapter_;
    MorphMap map_;
    juce::TextButton editButton_{"Edit shapes"};
    juce::ToggleButton toggle_{"Show Morph Map"};
    std::unique_ptr<ShapeMapController> controller_;
    synth::CardBlockFade editFade_;
    synth::CardBlockFade mapFade_;
    juce::Component::SafePointer<juce::DialogWindow> window_;
    int lastGeneration_ = -1;
    bool lastCustom_ = false; // the type as of the last sync
};

} // namespace synth::ui
