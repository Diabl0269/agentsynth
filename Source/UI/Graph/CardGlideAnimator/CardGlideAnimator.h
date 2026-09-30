#pragma once

// CardGlideAnimator.h
//
// Paint-only slide for cards the canvas moved as a side effect (make-room pushes, neighbour returns, Auto Arrange).
// Geometry is already final when this is armed; the real cards are hidden (alpha 0) and snapshots are drawn gliding
// from the old rect to the new one by GraphContentComponent::paintOverChildren. See CardGlideAnimator.cpp.

#include "UI/Layout/UIAnimation.h"
#include <cstdint>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace graph_editor_types {
struct VisibleCable;
}

class CardGlideAnimator {
public:
    /** One card: `nodeUid` is the module's node id, 0 for a macro card. */
    struct Entry {
        juce::Component* comp = nullptr;
        uint32_t nodeUid = 0;
    };

    /** What the owner lends: every card on the canvas, the snapshot scale, a repaint, and the VBlank updater. */
    struct Hooks {
        std::function<std::vector<Entry>()> cards;
        std::function<float()> snapshotScale;
        std::function<void()> repaint;
        juce::VBlankAnimatorUpdater* updater = nullptr;
    };

    struct Captured {
        juce::Component::SafePointer<juce::Component> comp;
        uint32_t nodeUid = 0;
        juce::Rectangle<int> bounds;
    };

    CardGlideAnimator() = default;
    ~CardGlideAnimator();
    CardGlideAnimator(const CardGlideAnimator&) = delete;
    CardGlideAnimator& operator=(const CardGlideAnimator&) = delete;

    /** Message thread only. Must be set before the first Scope is opened. */
    void setHooks(Hooks hooks);

    /** Bounds of every showing, non-empty card right now. */
    static std::vector<Captured> capture(const std::vector<Entry>& cards);

    /** Hides and snapshots every card whose bounds changed since `before`. Returns false (no-op) when none did. */
    bool arm(const std::vector<Captured>& before, const std::vector<Entry>& now, float snapshotScale);

    /** Eased progress 0..1. */
    void applyTweenAt(float t) noexcept;
    /** Restores every hidden card and drops all state. */
    void finish() noexcept;
    bool isLive() const noexcept { return !items_.empty(); }

    /** Draws the snapshots at their current rects (canvas coordinates). */
    void paint(juce::Graphics& g) const;
    /** Current minus final position of the gliding module card `nodeUid`; zero when it is not gliding. */
    juce::Point<float> offsetFor(uint32_t nodeUid) const noexcept;
    /** Shifts the endpoints of cables touching a gliding card. */
    void applyTo(std::vector<graph_editor_types::VisibleCable>& cables) const;
    /** Union of every gliding rect's from/to, for repaint. */
    juce::Rectangle<int> dirtyArea() const noexcept;

    /** The rect a gliding card is painted at right now, or empty when it is not gliding (test seam). */
    juce::Rectangle<int> currentRectFor(const juce::Component* comp) const noexcept;
    /** How many times arm() started a glide (test seam). */
    int armCount() const noexcept { return armCount_; }
    /** Repaint requests the driver made (test seam). */
    int repaintCount() const noexcept { return repaintCount_; }

    /** Captures on the outermost entry; arms and starts the driver on the outermost exit. */
    class Scope {
    public:
        explicit Scope(CardGlideAnimator& animator);
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        CardGlideAnimator& animator_;
    };

private:
    struct Item {
        juce::Component::SafePointer<juce::Component> comp;
        uint32_t nodeUid = 0;
        juce::Rectangle<int> from, to;
        juce::Image snapshot;
        float savedAlpha = 1.0f;
    };

    juce::Rectangle<float> currentRect(const Item& item) const noexcept;
    void startDriver();

    Hooks hooks_;
    std::vector<Item> items_;
    std::vector<Captured> before_;
    synth::ui::AnimationDriver driver_;
    float progress_ = 0.0f;
    int depth_ = 0;
    int armCount_ = 0;
    int repaintCount_ = 0;
};
