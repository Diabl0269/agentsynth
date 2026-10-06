#pragma once

// CardGlideAnimator.h
//
// Paint-only slide for cards the canvas moved as a side effect (make-room pushes, neighbour returns, Auto Arrange).
// Geometry is already final when this is armed; the real cards are hidden (alpha 0) and snapshots are drawn gliding
// from the old rect to the new one by GraphContentComponent::paintOverChildren. See CardGlideAnimator.cpp.

#include "UI/Layout/ExitEnterTimeline.h"
#include "UI/Layout/UIAnimation.h"
#include <cstdint>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace graph_editor_types {
struct VisibleCable;
}

namespace card_glide_detail {
/** The part of the canvas the card's viewer shows, in canvas coordinates; everything when it has no such viewer. */
juce::Rectangle<int> visibleCanvasArea(const juce::Component& card);
} // namespace card_glide_detail

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
        /** Whether a VBlank can reach the canvas (it is showing). Exit/enter ghosts are made only when it says yes,
         *  so a headless run lands on the final state synchronously. Unset: never. */
        std::function<bool()> canAnimate;
        /** The theme's accent, for the outline around a card an undo brought back. */
        std::function<juce::Colour()> accent;
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

    /** While a Scope is open: gives a card CREATED inside it (so it was not captured on entry) a starting rect, so
     *  the Scope's exit glides it from `from` to wherever it ends up. A library drop that snaps into place uses
     *  this to settle from where the cursor aimed. No-op outside a Scope. */
    void noteStartRect(juce::Component* comp, uint32_t nodeUid, juce::Rectangle<int> from);

    /** Hides and snapshots every card whose bounds changed since `before`. Returns false (no-op) when none did. */
    bool arm(const std::vector<Captured>& before, const std::vector<Entry>& now, float snapshotScale);

    /** Delete/undo ghosts (CardGlideAnimatorGhosts.cpp). While a Scope is open, snapshots `comp` so that, if the
     *  mutation removes its node, the card shrinks away instead of vanishing. No-op when not animating. */
    void noteExit(juce::Component* comp, uint32_t nodeUid);
    /** Test seam: animate even though the canvas is not showing. */
    void setForceAnimateForTest(bool force) noexcept { forceAnimate_ = force; }
    /** Test seam: advances the phased timeline to `elapsedMs` (what the driver does each frame). */
    void applyTimelineAtMs(double elapsedMs);
    /** Ghosts live right now: exits shrinking, enters growing or outlined (test seams). */
    int exitGhostCount() const noexcept;
    int enterGhostCount() const noexcept;
    /** The rect an exit/enter ghost for `nodeUid` is drawn at now (empty when none or not yet visible). */
    juce::Rectangle<float> ghostRectFor(uint32_t nodeUid) const noexcept;
    /** The phase timeline of the live animation. */
    const synth::ui::ExitEnterTimeline& timeline() const noexcept { return timeline_; }

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
        /** `restore`: an undo/redo, so every card is snapshotted for a possible exit and a card the restore creates
         *  grows in. */
        explicit Scope(CardGlideAnimator& animator, bool restore = false);
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        CardGlideAnimator& animator_;
    };

private:
    enum class Kind { Move, Exit, Enter };
    struct Candidate {
        uint32_t nodeUid = 0;
        juce::Rectangle<int> rect;
        juce::Image snapshot;
    };
    struct Item {
        Kind kind = Kind::Move;
        bool grown = false; // an Enter item whose card is live again; only its outline remains
        juce::Component::SafePointer<juce::Component> comp;
        uint32_t nodeUid = 0;
        juce::Rectangle<int> from, to;
        juce::Image snapshot;
        float savedAlpha = 1.0f;
    };

    juce::Rectangle<float> currentRect(const Item& item) const noexcept;
    void startDriver();
    bool canAnimate() const;
    void pruneItems();
    void landGhosts() noexcept;
    bool armGhosts(const std::vector<Captured>& before, const std::vector<Entry>& now, float snapshotScale);
    void paintGhost(juce::Graphics& g, const Item& item) const;
    bool hasMoveItems() const noexcept;

    Hooks hooks_;
    std::vector<Item> items_;
    std::vector<Captured> before_;
    synth::ui::AnimationDriver driver_;
    float progress_ = 0.0f;
    int depth_ = 0;
    int armCount_ = 0;
    std::vector<Candidate> candidates_;
    synth::ui::ExitEnterTimeline timeline_;
    synth::ui::ExitEnterTimeline::Frame frame_;
    bool phased_ = false;
    bool restoring_ = false;
    bool reducedMotion_ = false;
    bool forceAnimate_ = false;
    int repaintCount_ = 0;
};
