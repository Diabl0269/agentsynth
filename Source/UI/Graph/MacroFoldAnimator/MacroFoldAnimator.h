#pragma once

// MacroFoldAnimator.h
//
// Paint-only fold of a macro's modules into its closed card, and out of it again (docs/layout/animation.md "Macro
// fold"). The model change (collapse, expand, undo, redo) is already final and synchronous when this is armed; the
// real cards that should not show yet are held invisible and ghosts of them are drawn flying between their place on
// the canvas and their preview box on the card, on top of the canvas by GraphContentComponent::paintOverChildren.
// The timing is the pure Timeline in MacroFoldTimeline.h. Owned by CardGlideAnimator, which lends it its hooks.

#include "MacroFoldTimeline.h"
#include "UI/Graph/CableColour.h"
#include "UI/Graph/GraphEditor/GraphEditorTypes.h"
#include "UI/Layout/UIAnimation.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <vector>

class MacroFoldAnimator {
public:
    using VisibleCable = graph_editor_types::VisibleCable;

    /** What the owner lends: a repaint, the VBlank updater, whether the canvas shows, and the canvas' own drawing. */
    struct Hooks {
        juce::VBlankAnimatorUpdater* updater = nullptr;
        std::function<bool()> canAnimate;
        /** Repaints the whole canvas and drops its cable memo. */
        std::function<void()> repaint;
        /** Repaints only `area` (canvas coordinates). Unset: every frame uses `repaint`. */
        std::function<void(juce::Rectangle<int>)> repaintArea;
        /** A copy of the cables drawn on the canvas right now. */
        std::function<std::vector<VisibleCable>()> visibleCables;
        std::function<juce::Colour(synth::ui::ModuleCategory)> categoryColour;
        /** Strokes `cable` (its ends already moved) with the canvas' wire look at `alpha`. */
        std::function<void(juce::Graphics&, const VisibleCable&, float alpha)> paintCable;
    };

    /** One module of the macro: where its card stands on the open canvas and where its box sits on the closed card. */
    struct Module {
        uint32_t nodeUid = 0;
        synth::ui::ModuleCategory category = synth::ui::ModuleCategory::Utility;
        juce::Rectangle<int> rect;
        juce::Rectangle<float> box;
        juce::Image picture; // the card's own raster; null draws a plain box
        juce::Component::SafePointer<juce::Component> comp;
    };

    /** One macro's fold. Collapse: `cables` are the ones with both ends on a module; expand: the ones touching one. */
    struct Plan {
        juce::String macroId;
        bool collapsing = true;
        juce::Colour colour;
        juce::Rectangle<int> hull; // the open border
        juce::Rectangle<int> card; // the closed card
        juce::Component::SafePointer<juce::Component> cardComp;
        std::vector<Module> modules;
        std::vector<VisibleCable> cables;
        /** The open macro's port widgets: held back until the border has grown to hold them. */
        std::vector<juce::Component::SafePointer<juce::Component>> ports;
    };

    /** What is captured before a collapse or expand changes the canvas, to be turned into a Plan once it has: for an
     *  open macro the modules' rects, pictures, the border and the inner cables; for a closed one the card and boxes.
     */
    struct Before {
        bool wasCollapsed = false;
        juce::Rectangle<int> hull, card;
        std::vector<Module> modules;
        std::vector<VisibleCable> cables;
    };

    /** A cable as drawn this frame. */
    struct DrawnCable {
        VisibleCable cable;
        float alpha = 1.0f;
    };

    MacroFoldAnimator() = default;
    ~MacroFoldAnimator();
    MacroFoldAnimator(const MacroFoldAnimator&) = delete;
    MacroFoldAnimator& operator=(const MacroFoldAnimator&) = delete;

    /** Message thread only. Must be set before the first arm(). */
    void setHooks(Hooks hooks);
    const Hooks& hooks() const noexcept { return hooks_; }

    /** The animation lands at once (arm() returns false) unless the canvas shows (or a test forces it) and animations
     *  are not Off. Lands any fold already running first. */
    bool arm(std::vector<Plan> plans);
    bool isLive() const noexcept { return !lives_.empty(); }
    /** Whether arm() would animate right now: the canvas shows (or a test forces it) and animations are not Off. A
     *  caller skips capturing pictures and cables when this is false. */
    bool wouldAnimate() const;
    /** Shows every held card, drops all state and repaints. */
    void land() noexcept;

    /** Advances the clock to `ms` (what the driver does each frame) and lands when everything has settled. */
    void applyAtMs(double ms);
    /** Test seam: one driver frame at linear driver value `t`, including its repaint request. */
    void stepFrameForTest(float t);
    double elapsedMs() const noexcept { return elapsed_; }
    double totalMs() const noexcept { return total_; }
    bool isReduced() const noexcept { return reduced_; }
    void setForceAnimateForTest(bool force) noexcept { forceAnimate_ = force; }

    /** Modules still flying or waiting to land, over every macro. */
    int ghostCount() const noexcept;
    /** Every module of `macroId` as drawn now (a landed one at its place), canvas coordinates; empty if none folds. */
    std::vector<juce::Rectangle<float>> moduleRects(const juce::String& macroId) const;
    /** The rect the module with `nodeUid` is drawn at now; empty when it is not folding. */
    juce::Rectangle<float> moduleRectFor(uint32_t nodeUid) const;
    /** The border drawn around `macroId` now, holding every module; nothing when it does not fold. */
    std::optional<juce::Rectangle<float>> outlineFor(const juce::String& macroId) const;
    /** The same for an expanding macro, as the integer rect its border is painted with. */
    std::optional<juce::Rectangle<int>> expandOutlineFor(const juce::String& macroId) const;
    /** Whether the module's real card is still held back. */
    bool isHeld(uint32_t nodeUid) const;

    /** An expanding macro's cables are drawn here (growing out of their ports), not by the canvas: removes them. */
    void applyTo(std::vector<VisibleCable>& cables) const;
    /** The cables the fold draws itself this frame. */
    std::vector<DrawnCable> drawnCables() const;
    /** A copy of the canvas' cables (empty without the hook). */
    std::vector<VisibleCable> currentCables() const;

    void paint(juce::Graphics& g) const;
    /** What the fold draws now, for repaint. */
    juce::Rectangle<int> drawnArea() const;

    /** The card raster of `comp`, or a null image when it has none (never renders a card). */
    static juce::Image cachedPicture(juce::Component& comp);
    /** The dashed macro border the expanded canvas draws. */
    static void paintDashedBorder(juce::Graphics& g, juce::Rectangle<float> border, juce::Colour colour,
                                  bool emphasised);

private:
    struct Live {
        Plan plan;
        synth::ui::macro_fold::Timeline timeline;
        std::vector<char> landed;
    };

    float handover(const Live& live) const noexcept;
    juce::Rectangle<float> moduleRect(const Live& live, size_t index) const noexcept;
    std::vector<juce::Rectangle<float>> moduleRects(const Live& live) const;
    juce::Rectangle<float> outline(const Live& live) const noexcept;
    int indexOf(const Live& live, uint32_t nodeUid) const noexcept;
    std::optional<DrawnCable> expandCable(const Live& live, const VisibleCable& cable) const;
    std::optional<DrawnCable> collapseCable(const Live& live, const VisibleCable& cable) const;
    void paintModule(juce::Graphics& g, const Live& live, size_t index) const;
    void applySideEffects();
    void frameAt(float t);
    void release(bool stopDriver) noexcept;
    bool canAnimate() const;

    Hooks hooks_;
    std::vector<Live> lives_;
    synth::ui::AnimationDriver driver_;
    double elapsed_ = 0.0;
    double total_ = 0.0;
    bool reduced_ = false;
    bool forceAnimate_ = false;
    juce::Rectangle<int> lastDrawn_;
};
