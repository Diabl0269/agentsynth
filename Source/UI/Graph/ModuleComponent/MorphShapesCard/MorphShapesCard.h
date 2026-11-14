#pragma once

// MorphShapesCard.h -- a card's Custom section for a module whose Custom wave or response is a list of shapes on a
// morph map (the Filter's Custom type, the LFO's Custom shape): an "Edit shapes" button, the morph map and the Show
// Morph Map pill. A collaborator of ModuleComponent, which hands it the few things it needs through `Host`.
#include "Modules/CardViewState.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/CardBody/CardBlockFade.h"
#include "UI/Graph/CardWidgets/CardControlGestures.h"
#include "UI/Layout/ActionTexts.h"
#include "UI/Layout/KeyGlide/RestoreWatch.h"
#include "UI/Library/SoundBrowser/ShapeDropTarget.h"
#include "UI/ModuleViews/CurveEditor/CurveEditorComponent.h"
#include "UI/ModuleViews/MorphMap/MorphMap.h"
#include "UI/ModuleViews/ShapeEditor/ModuleShapesAdapter.h"
#include "UI/ModuleViews/ShapeEditor/ShapeMapController.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>

class ShortcutManager;

namespace synth::ui {

class MorphShapesCard : public ShapeDropTarget {
public:
    struct Host {
        juce::Component* card = nullptr;
        std::function<void()> relayout;
        std::function<void()> makeRoom;
        bool* settling = nullptr;
        std::function<void(juce::PopupMenu&)> showMenu;
        /** Shows a menu beside `screenArea` (the Pick shape button); unset falls back to showMenu. */
        std::function<void(juce::PopupMenu&, juce::Rectangle<int> screenArea)> showMenuAt;
        /** Opens the control menu of the parameter behind raw channel `channel` beside `screenArea`; unset shows none.
         */
        std::function<void(int channel, juce::Rectangle<int> screenArea)> stripMenu;
        std::function<void(const std::function<void(synth::CardViewState&)>&)> rememberView;
        std::function<void(bool)> hideMorphKnobs;
        juce::String mapPlace;               ///< where the map sits, for the pill's tooltip ("under the response view")
        std::function<bool()> customActive;  ///< the module is on its Custom type / shape
        std::function<juce::String()> title; ///< the card's title, for the window
        /** The modulation each routing on raw channel `channel` brings (empty for none); unset shows none. */
        std::function<std::vector<MorphMap::StripModulation>(int channel)> stripModulation;
        /** The tooltip of the mod dot of `channel` (empty when it has none); unset gives the map's own. */
        std::function<juce::String(int channel)> modDotTooltip;
        /** Where the shape window keeps its grid and spectrum choices; unset (or null) remembers nothing. */
        std::function<juce::ApplicationProperties*()> appProperties;
        /** The user's key bindings, for the map's Tab walk; unset walks with Tab and Shift+Tab. */
        std::function<const ShortcutManager*()> shortcuts;
        float landingDotDiameter = 7.0f; ///< the dot the canvas paints where a cable lands on a slider
        bool curveView = false;          ///< offers the small curve view of the shape under the cursor and its pill
    };

    /** The card, module and adapter must outlive this (detach() first). Message thread only for every member. */
    MorphShapesCard(Host host, ModuleBase& module, std::shared_ptr<ModuleShapesAdapter> adapter, int morphXChannel,
                    int morphYChannel);
    ~MorphShapesCard() override;

    /** Re-reads the type, the shape count and the remembered choice and shows or hides the parts. */
    void sync();
    /** The card's gated tick: follows undo, the editor window, automation and the knobs. */
    void poll();
    /** Lays the parts out from `y`, `topGap` (faded with the button) above the first, and returns the y below them. */
    int layout(int y, int x, int width, bool apply, int topGap = 0);
    /** A dragged library shape is taken while the module is on its Custom type and the kinds match. */
    bool acceptsShapeDrop(synth::shapes::ShapeKind kind) const override;
    void shapeDragOver(bool over, const juce::String& name) override;
    /** Replaces the curve of the shape nearest the cursor, as one undo step. */
    void shapeDropped(const synth::shapes::LibraryShape& shape) override;
    /** The pointer is over the card carrying a shape it would take. */
    bool isShapeDropOver() const noexcept { return dropOver_; }
    /** Paints the full drop highlight and the label saying what the drop does, over the card's `bounds`. */
    void paintShapeDropHint(juce::Graphics& g, juce::Rectangle<float> bounds) const;
    /** Closes the editor window and drops everything that references the module. */
    void detach();

    /** One window per card; asking again raises it and selects the shape. Null when a test hook took the window. */
    juce::DialogWindow* openEditor(int shapeIndex);
    /** The shapes the map shows (the editor's list). */
    int getNumShapes() const;
    /** The shape the cursor is closest to (the one that dominates the blend). */
    int nearestShapeToCursor() const;
    /** The shape Pick shape and Edit shapes act on: the selected dot, else the one nearest the cursor (also when the
     *  cursor itself is the selection). */
    int targetShapeIndex() const;

    /** The map is on the card (shown or fading in): its sliders then stand in for the Morph X / Y knobs. */
    bool isMapShown() const;
    /** The raw CV channel of the slider under `cardPoint`, if the map is shown and a slider is there. */
    std::optional<int> channelAt(juce::Point<int> cardPoint) const;
    /** Where a cable for raw channel `channel` lands on the map's slider, card coordinates. */
    std::optional<juce::Point<float>> anchorFor(int channel) const;
    juce::Rectangle<int> stripBoundsFor(int channel) const;
    /** Lights the slider of `channel` as the pending drop target (-1 clears it). */
    void setDropChannel(int channel);
    /** The slider of raw channel `channel`, which stands in for the hidden knob; null unless the map is shown. */
    juce::Component* stripControlFor(int channel) const;
    /** The gestures a press on that slider's modulation bar or landing dot runs, for the card to wire as it does a
     *  fader's (CardControlGestures); the coordinates are the map's. */
    CardControlGestures& stripGestures(int channel);
    MorphMap::Axis axisForChannel(int channel) const;
    /** The raw CV channel of the slider of `axis` (-1 for none). */
    int axisChannel(MorphMap::Axis axis) const noexcept;

    /** The small curve view: the shape under the cursor, editable when the cursor is on a dot, else the blend it
     *  hears, read-only. Present only when the Host asked for it. */
    CurveEditorComponent& getCurveView() noexcept { return curveView_; }
    juce::ToggleButton& getCurveToggle() noexcept { return curveToggle_; }
    bool hasCurveView() const noexcept { return host_.curveView; }
    bool isCurveViewShown() const;
    /** The shape the curve view edits, or -1 while it shows the read-only blend (cursor between the dots). */
    int curveViewShape() const noexcept { return curveShape_; }

    MorphMap& getMap() noexcept { return map_; }
    juce::TextButton& getEditButton() noexcept { return editButton_; }
    juce::TextButton& getPickButton() noexcept { return pickButton_; }
    /** Opens the shape picker beside the Pick shape button (what a press of the button does). */
    void openPicker();
    /** Applies a library shape picked from the picker: the target shape takes its curve, as one undo step. */
    void applyPickedShape(const synth::shapes::LibraryShape& shape);
    juce::ToggleButton& getToggle() noexcept { return toggle_; }
    ShapeMapController& getController() noexcept { return *controller_; }
    ModuleShapesAdapter& getAdapter() noexcept { return *adapter_; }

private:
    class StripGestures : public CardControlGestures {
    public:
        bool press(const juce::MouseEvent& e) { return claimMouseDown(e); }
        bool forward(const juce::MouseEvent& e, int phase) { return forwardClaimed(e, phase); }
        void hover(bool entered) { notifyHover(entered); }
    };

    bool mapWanted() const;
    void replaceShapeCurve(int target, const synth::shapes::LibraryShape& shape);
    bool routeMapPress(const juce::MouseEvent& e, int phase);
    void routeStripHover(MorphMap::Axis axis);
    void openStripMenu(MorphMap::Axis axis, juce::Rectangle<int> screenArea);
    void pushStripModulation();
    juce::String stripTooltipAt(juce::Point<float> point) const;
    // MorphShapesCardCurve.cpp
    void setUpCurveView();
    bool curveWanted() const;
    void syncCurveView(bool force);
    void writeCurveView();
    void describeCurveView(int shape, int count);

    Host host_;
    ModuleBase& module_;
    std::shared_ptr<ModuleShapesAdapter> adapter_;
    int morphXChannel_;
    int morphYChannel_;
    MorphMap map_;
    juce::TextButton editButton_{"Edit shapes"};
    juce::TextButton pickButton_{actiontext::kPickShape};
    juce::ToggleButton toggle_{"Show Morph Map"};
    std::unique_ptr<ShapeMapController> controller_;
    synth::CardBlockFade editFade_;
    synth::CardBlockFade mapFade_;
    synth::CardBlockFade curveFade_;
    CurveEditorComponent curveView_;
    juce::ToggleButton curveToggle_{"Show Curve"};
    int curveShape_ = -1; ///< the shape the view edits; -1 while it shows the blend
    int lastCurveGeneration_ = -1;
    juce::Point<float> lastCurveCursor_{-1.0f, -1.0f};
    bool curveGestureOpen_ = false;
    RestoreWatch curveWatch_;
    StripGestures stripGestures_[2]; // X, Y
    int pressedStrip_ = -1;          // the strip whose gesture owns the press in flight
    juce::Component::SafePointer<juce::DialogWindow> window_;
    int lastGeneration_ = -1;
    bool lastCustom_ = false; // the type as of the last sync
    bool dropOver_ = false;
    juce::String dropName_;
};

} // namespace synth::ui
