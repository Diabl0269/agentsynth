#pragma once

// MorphShapesCard.h -- a card's Custom section for a module whose Custom wave or response is a list of shapes on a
// morph map (the Filter's Custom type, the LFO's Custom shape): an "Edit shapes" button, the morph map and the Show
// Morph Map pill. A collaborator of ModuleComponent, which hands it the few things it needs through `Host`.
#include "Modules/CardViewState.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/CardBody/CardBlockFade.h"
#include "UI/ModuleViews/MorphMap/MorphMap.h"
#include "UI/ModuleViews/ShapeEditor/ModuleShapesAdapter.h"
#include "UI/ModuleViews/ShapeEditor/ShapeMapController.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>

namespace synth::ui {

class MorphShapesCard {
public:
    struct Host {
        juce::Component* card = nullptr;
        std::function<void()> relayout;
        std::function<void()> makeRoom;
        bool* settling = nullptr;
        std::function<void(juce::PopupMenu&)> showMenu;
        std::function<void(const std::function<void(synth::CardViewState&)>&)> rememberView;
        std::function<void(bool)> hideMorphKnobs;
        juce::String mapPlace;               ///< where the map sits, for the pill's tooltip ("under the response view")
        std::function<bool()> customActive;  ///< the module is on its Custom type / shape
        std::function<juce::String()> title; ///< the card's title, for the window
    };

    /** The card, module and adapter must outlive this (detach() first). Message thread only for every member. */
    MorphShapesCard(Host host, ModuleBase& module, std::shared_ptr<ModuleShapesAdapter> adapter, int morphXChannel,
                    int morphYChannel);
    ~MorphShapesCard();

    /** Re-reads the type, the shape count and the remembered choice and shows or hides the parts. */
    void sync();
    /** The card's gated tick: follows undo, the editor window, automation and the knobs. */
    void poll();
    /** Lays the parts out from `y` and returns the y below them. */
    int layout(int y, int x, int width, bool apply);
    /** Closes the editor window and drops everything that references the module. */
    void detach();

    /** One window per card; asking again raises it and selects the shape. Null when a test hook took the window. */
    juce::DialogWindow* openEditor(int shapeIndex);
    /** The shapes the map shows (the editor's list). */
    int getNumShapes() const;
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
    ModuleShapesAdapter& getAdapter() noexcept { return *adapter_; }

private:
    bool mapWanted() const;
    MorphMap::Axis axisForChannel(int channel) const;

    Host host_;
    ModuleBase& module_;
    std::shared_ptr<ModuleShapesAdapter> adapter_;
    int morphXChannel_;
    int morphYChannel_;
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
