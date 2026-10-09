#pragma once

// InsertGap.h (docs/layout/layout.md#making-room-for-a-module-dropped-between-others): the live half of "a module
// dragged between two cards pushes the cards after it aside". While a drag hovers over a gap the neighbours slide
// aside (real moves, gliding), moving on closes it again, and the drop re-makes the same gap inside its own undo
// record so the whole insert is one step. The geometry is the pure InsertGapPlan; the moves are
// MacroGroupController::moveUnitBy and the glide is CardGlideAnimator. MacroGroupController owns one (insertGap()).

#include "UI/Graph/CardGlideAnimator/CardGlideAnimator.h"
#include "UI/Layout/InsertGap/InsertGapPlan.h"
#include "UI/Layout/ReorderDrag/ReorderCancelKey.h"
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <optional>
#include <vector>

class GraphCanvasHost;
class MacroGroupController;

class InsertGap {
public:
    /** What the canvas lends. All optional. */
    struct Hooks {
        /** Pictures the macro borders now; the call it returns glides them to wherever they are by then. */
        std::function<std::function<void()>()> beginBorderGlide;
        /** After cards moved: lets the canvas frame grow to hold them. */
        std::function<void()> afterMove;
        /** Esc is listened for on this component's window while a gap is open (focus may be anywhere). */
        juce::Component* keyHost = nullptr;
    };

    /** One drag tick. `pointer` is the centre of the dragged card or library ghost, in canvas coordinates. */
    struct Hover {
        juce::String container; // the macro a library module will join; empty for the top level
        juce::String selfKey;   // a dragged card's own unit key ("n:<uid>"), whose own macro is then the container
        juce::Point<int> pointer;
        juce::Point<int> size;          // the card's (w, h)
        juce::Rectangle<int> selfStart; // where a dragged card was picked up; empty for a library drag
    };

    InsertGap(GraphCanvasHost& host, MacroGroupController& macros);
    ~InsertGap();
    InsertGap(const InsertGap&) = delete;
    InsertGap& operator=(const InsertGap&) = delete;

    void setHooks(Hooks hooks);

    /** Message thread only. Opens, keeps, moves or closes the gap for where the drag is now. */
    void hover(const Hover& hover);
    /** The drag ended without a drop (or a new one began): every card the gap moved goes back. */
    void endSession();
    /** Esc: closes an open gap and keeps it closed for the rest of this drag. False when none was open. */
    bool cancel();
    /** The graph was replaced under the gap (undo, load): forgets it without moving anything. */
    void forget() noexcept;

    bool isOpen() const noexcept { return plan_.has_value(); }
    /** The open gap, or null. */
    const synth::insert_gap::Plan* plan() const noexcept { return plan_ ? &*plan_ : nullptr; }
    const juce::String& container() const noexcept { return container_; }

    /** Keyboard insert: prepares a commit that puts a `size` card right after `afterKey` in its row, inside
     *  `container`. False when nothing follows it in its row (the caller places the card itself). */
    bool armAfter(const juce::String& container, const juce::String& afterKey, juce::Point<int> size);
    /** Drops a prepared commit that never ran (the add was refused). */
    void clearPending() noexcept;
    bool hasPending() const noexcept { return pending_.has_value(); }

    /** Brackets a drop that lands in the gap: construct it BEFORE the drop's undo record (it puts every card back
     *  home, so the record's "before" is the layout without the gap) and keep it alive across the record; inside it,
     *  land() makes the gap again. Its end lets the glide run. No-op when no gap is open or prepared. */
    class Commit {
    public:
        explicit Commit(InsertGap& gap);
        ~Commit();
        Commit(const Commit&) = delete;
        Commit& operator=(const Commit&) = delete;

    private:
        InsertGap& gap_;
        std::optional<CardGlideAnimator::Scope> glide_;
        std::function<void()> borders_;
        bool owns_ = false; // a Commit made while another runs does nothing
    };

    /** True between a Commit's start and end. */
    bool isCommitting() const noexcept { return committing_; }
    /** Where the card lands (the slot of the gap being committed), valid while committing. */
    juce::Point<int> pendingSlot() const noexcept { return pendingSlot_; }
    /** Inside the drop's undo record: makes the gap for `nodeId`'s card (its real `size`), remembers what it pushed on
     *  that card so deleting it brings them home, and answers where the card goes. Empty when the gap is gone. */
    std::optional<juce::Point<int>> land(juce::AudioProcessorGraph::NodeID nodeId, juce::Point<int> size);

private:
    struct Applied {
        juce::String key;
        juce::Point<int> delta;
        juce::String container; // the level the unit sits at
    };

    std::vector<synth::LayoutUtil::LayoutUnit> unitsAt(const juce::String& container,
                                                       const juce::String& without) const;
    bool selfCanInsert(const juce::String& selfKey) const;
    void retarget(const Hover& hover, const std::optional<synth::insert_gap::Target>& target);
    void close();
    void apply(const synth::insert_gap::Plan& plan, const juce::String& container, const juce::String& without);
    bool revert();
    void snapshotDock();
    void restoreDock();
    void armEscape();

    GraphCanvasHost& host_;
    MacroGroupController& macros_;
    Hooks hooks_;
    synth::ui::ReorderCancelKey escape_;

    std::optional<synth::insert_gap::Plan> plan_; // the gap open now
    juce::String container_, selfKey_;
    juce::Point<int> size_;
    juce::Rectangle<int> hold_;    // the pointer keeps the open gap while it stays in here
    std::vector<Applied> applied_; // every move the open gap made, in order
    std::vector<std::pair<juce::AudioProcessorGraph::NodeID, juce::Point<int>>> dock_; // where the dock stood
    bool suppressed_ = false;
    juce::String idle_; // the last target that needed no room, so a pointer resting on it plans nothing again

    std::optional<synth::insert_gap::Target> pending_; // a commit's target
    juce::String pendingContainer_;
    juce::Point<int> pendingSlot_, pendingSize_;
    bool committing_ = false;
};
