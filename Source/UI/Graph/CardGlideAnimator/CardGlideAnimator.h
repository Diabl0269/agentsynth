#pragma once

// CardGlideAnimator.h
//
// Paint-only slide for cards the canvas moved as a side effect (make-room pushes, neighbour returns, Auto Arrange).
// Geometry is already final when this is armed; the real cards are hidden (alpha 0) and snapshots are drawn gliding
// from the old rect to the new one by GraphContentComponent::paintOverChildren. See CardGlideAnimator.cpp.

#include "UI/Graph/CableColour.h"
#include "UI/Layout/ExitEnterTimeline.h"
#include "UI/Layout/UIAnimation.h"
#include <cstdint>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <set>
#include <utility>
#include <vector>

namespace graph_editor_types {
struct VisibleCable;
}
class MacroFoldAnimator;

namespace card_glide_detail {
/** The part of the canvas the card's viewer shows, in canvas coordinates; everything when it has no such viewer. */
juce::Rectangle<int> visibleCanvasArea(const juce::Component& card);
} // namespace card_glide_detail

class CardGlideAnimator {
public:
    /** The ghost identity of a macro card (macroKey) and of an open macro's border (borderKey): node ids never reach
     *  the top two bits, so neither collides with a module. */
    static uint32_t macroKey(const juce::String& macroId) noexcept {
        return 0x80000000u | (static_cast<uint32_t>(macroId.hashCode()) & 0x3fffffffu);
    }
    static uint32_t borderKey(uint32_t macroCardKey) noexcept { return macroCardKey | 0x40000000u; }
    static bool isMacroKey(uint32_t key) noexcept { return (key & 0x80000000u) != 0; }

    /** What a macro looks like on the canvas, captured while the canvas shows it so that a macro the change removes can
     *  leave with its cards: `open` borders carry the geometry and look of the dashed border, the name chip and the
     *  two port strips; `members` are the node ids of everything drawn inside it. */
    struct Border {
        uint32_t key = 0; // macroKey of the macro
        bool open = false;
        juce::Rectangle<int> hull, chip;
        juce::Colour colour, stripFill;
        juce::String name;
        int inWidth = 0, outWidth = 0;
        std::vector<uint32_t> members;
    };

    /** One card: `nodeUid` is the module's node id; a macro card carries its macroKey. */
    struct Entry {
        juce::Component* comp = nullptr;
        uint32_t nodeUid = 0;
    };

    /** What the owner lends: every card on the canvas, the snapshot scale, a repaint, and the VBlank updater. */
    struct Hooks {
        std::function<std::vector<Entry>()> cards;
        /** Every macro on the canvas as it looks now (see Border). Unset: macros leave without their border. */
        std::function<std::vector<Border>()> borders;
        std::function<float()> snapshotScale;
        /** Repaints the whole canvas and drops its cable memo. */
        std::function<void()> repaint;
        /** Repaints only `area` (canvas coordinates) and keeps the cable memo. Unset: every frame uses `repaint`. */
        std::function<void(juce::Rectangle<int>)> repaintArea;
        /** The canvas's cable memo while it is valid, else nullptr. Unset: every frame uses `repaint`. */
        std::function<std::vector<graph_editor_types::VisibleCable>*()> liveCables;
        juce::VBlankAnimatorUpdater* updater = nullptr;
        /** Whether a VBlank can reach the canvas (it is showing). Exit/enter ghosts are made only when it says yes,
         *  so a headless run lands on the final state synchronously. Unset: never. */
        std::function<bool()> canAnimate;
        /** The theme's accent, for the outline around a card an undo brought back. */
        std::function<juce::Colour()> accent;
        /** Lent on to the macro fold (MacroFoldAnimator.h): a copy of the cables drawn now, a module category's
         *  colour, and the canvas' wire look for a cable the fold draws itself. */
        std::function<std::vector<graph_editor_types::VisibleCable>()> visibleCables;
        std::function<juce::Colour(synth::ui::ModuleCategory)> categoryColour;
        std::function<void(juce::Graphics&, const graph_editor_types::VisibleCable&, float)> paintCable;
    };

    struct Captured {
        juce::Component::SafePointer<juce::Component> comp;
        uint32_t nodeUid = 0;
        juce::Rectangle<int> bounds;
    };

    CardGlideAnimator();
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

    /** While a restore Scope is open: snapshots every on-screen card for a possible exit. Call it before tearing all
     * the cards down; a restore that frees no node never does, so it snapshots nothing. No-op otherwise. */
    void noteExitsBeforeTeardown();
    /** Delete/undo ghosts (CardGlideAnimatorGhosts.cpp). While a Scope is open, snapshots `comp` so that, if the
     *  mutation removes its node, the card shrinks away instead of vanishing. No-op when not animating. */
    void noteExit(juce::Component* comp, uint32_t nodeUid);
    /** While a Scope is open, remembers how every macro border looks so that, if the mutation removes a macro with all
     *  its modules, the border shrinks away with them. Once per Scope; a restore Scope does it on entry. */
    void noteMacroBorders();
    /** Whether the real border of `macroId` stays unpainted: an undo is growing it back as a ghost. */
    bool isBorderHeld(const juce::String& macroId) const noexcept;
    /** Border ghosts live right now (test seam). */
    int borderGhostCount() const noexcept;
    /** Test seam: animate even though the canvas is not showing. */
    void setForceAnimateForTest(bool force) noexcept { forceAnimate_ = force; }
    /** The fold of a macro's modules into its closed card and back, drawn by the same overlay. */
    MacroFoldAnimator& fold() noexcept;
    const MacroFoldAnimator& fold() const noexcept;
    /** Drops the delete/undo ghosts of the cards in `nodeUids` and puts a hidden one back: a macro fold animates
     *  them instead (an undo of a collapse would otherwise also shrink every member away). */
    void dropGhostsFor(const std::vector<uint32_t>& nodeUids);
    /** Test seam: advances the phased timeline to `elapsedMs` (what the driver does each frame). */
    void applyTimelineAtMs(double elapsedMs);
    /** Ghosts live right now: exits shrinking, enters growing or outlined (test seams). */
    int exitGhostCount() const noexcept;
    int enterGhostCount() const noexcept;
    /** The rect an exit/enter ghost for `nodeUid` is drawn at now (empty when none or not yet visible). */
    juce::Rectangle<float> ghostRectFor(uint32_t nodeUid) const noexcept;
    /** The phase timeline of the live animation. */
    const synth::ui::ExitEnterTimeline& timeline() const noexcept { return timeline_; }

    /** Test seam: one driver frame at driver value `t` (the eased glide, or the phased timeline's linear 0..1),
     *  including the repaint request, exactly as the VBlank runs it. */
    void stepFrameForTest(float t);
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
    void applyTo(std::vector<graph_editor_types::VisibleCable>& cables);
    /** Union of every gliding rect's from/to, for repaint. */
    juce::Rectangle<int> dirtyArea() const noexcept;

    /** The rect a gliding card is painted at right now, or empty when it is not gliding (test seam). */
    juce::Rectangle<int> currentRectFor(const juce::Component* comp) const noexcept;
    /** Card pictures taken for a glide or ghost, and how many of them had to be rendered rather than taken from the
     *  card's own raster cache (test seams). */
    int snapshotCount() const noexcept { return snapshotCount_; }
    int renderedSnapshotCount() const noexcept { return renderedSnapshotCount_; }
    /** The canvas area the last driver frame repainted; empty when it repainted the whole canvas (test seam). */
    juce::Rectangle<int> lastFrameArea() const noexcept { return lastFrameArea_; }
    /** How many times arm() started a glide (test seam). */
    int armCount() const noexcept { return armCount_; }
    /** Repaint requests the driver made (test seam). */
    int repaintCount() const noexcept { return repaintCount_; }

    /** Captures on the outermost entry; arms and starts the driver on the outermost exit. */
    class Scope {
    public:
        /** `restore`: an undo/redo, so a card the restore removes shrinks away (see noteExitsBeforeTeardown) and one
         *  it creates grows in. */
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
        juce::Component::SafePointer<juce::Component> comp;
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
        float exitStart = 0.0f; // an Exit ghost an interruption carried over: how far it had already shrunk
        std::shared_ptr<const Border> border; // a macro border ghost: drawn from this, no card behind it
    };

    juce::Rectangle<float> currentRect(const Item& item) const noexcept;
    void startDriver();
    void frameAt(float t);
    void finishFrame();
    void requestFrameRepaint(juce::Rectangle<int> drawnBefore);
    juce::Rectangle<int> followCables(std::vector<graph_editor_types::VisibleCable>& cables);
    std::vector<std::pair<uint32_t, juce::Point<float>>> currentOffsets() const;
    juce::Image snapshotOf(juce::Component& comp, float scale);
    void noteRestoreExits(const std::vector<Entry>& now);
    bool canAnimate() const;
    void pruneItems();
    void landGhosts() noexcept;
    float exitProgress(const Item& item) const noexcept;
    void dropCarriedExitsThatReturn(const std::vector<Entry>& now);
    bool armGhosts(const std::vector<Captured>& before, const std::vector<Entry>& now, float snapshotScale);
    void paintGhost(juce::Graphics& g, const Item& item) const;
    bool hasMoveItems() const noexcept;
    void armBorderGhosts(const std::vector<Entry>& now, bool& anyExit, bool& anyEnter);
    static void paintBorder(juce::Graphics& g, const Border& border, float scale, float alpha);

    Hooks hooks_;
    std::unique_ptr<MacroFoldAnimator> fold_;
    std::vector<Item> items_;
    std::vector<Captured> before_;
    synth::ui::AnimationDriver driver_;
    float progress_ = 0.0f;
    int depth_ = 0;
    int armCount_ = 0;
    std::vector<Candidate> candidates_;
    std::vector<Border> borders_;          // captured by noteMacroBorders
    std::set<uint32_t> preexistingMacros_; // every macro card there when the Scope opened, shown or not
    synth::ui::ExitEnterTimeline timeline_;
    synth::ui::ExitEnterTimeline::Frame frame_;
    bool phased_ = false;
    bool restoring_ = false;
    bool reducedMotion_ = false;
    bool forceAnimate_ = false;
    int repaintCount_ = 0;
    int snapshotCount_ = 0;
    int renderedSnapshotCount_ = 0;
    juce::Rectangle<int> lastFrameArea_;
    // The glide offsets the cable memo holds right now, so a frame moves each cable by the change only.
    std::vector<std::pair<uint32_t, juce::Point<float>>> applied_;
};
