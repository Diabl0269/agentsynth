#pragma once

#include <algorithm>
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <vector>

namespace synth {

/** A named, coloured, collapsible grouping of graph nodes on the canvas — VISUAL AND
 *  ORGANISATIONAL ONLY (P8-12; docs/layout_selection_canvas.md). A Macro adds no ports, no
 *  graph edges, and no processing of its own: it is a persisted selection plus presentation.
 *  Configurable I/O (turning a Macro into something with its own ports) is a separate, later
 *  feature (P8-15) — keeping the two apart is deliberate.
 *
 *  Membership is by node UUID (the same persistent "uuid" ModuleBase::setNodeUuid mirrors into
 *  the processor), never by juce::AudioProcessorGraph::NodeID — a NodeID is only valid for the
 *  lifetime of one loaded graph and is meaningless once serialised (Source/CLAUDE.md's
 *  uuid-mirroring invariant).
 *
 *  Flat model: a node already in a macro cannot be grouped into a second one. Nested macros are
 *  out of scope for P8-12 — see GraphEditor::groupSelectionIntoMacro, which refuses (with a
 *  status message, not a silent no-op) rather than doing something ad hoc. */
struct Macro {
    juce::String id; // opaque, unique within a MacroSet — never mirrored into a processor
    juce::String name;
    juce::Colour colour{0xff5a7dff};
    bool collapsed = false;

    // The collapsed card's own canvas rectangle. Authoritative ONLY while collapsed: a card is
    // deliberately small and independent of however far-flung its (hidden) members are, and
    // moving the card drags every member along by the same delta (GraphEditor's macro drag
    // reuses beginSelectionDrag/dragSelectionBy/finalizeSelectionDrag for that). Meaningless
    // while expanded — nothing reads it then.
    juce::Rectangle<int> bounds;

    std::vector<juce::String> members; // node uuids

    bool hasMember(const juce::String& memberUuid) const {
        return std::find(members.begin(), members.end(), memberUuid) != members.end();
    }
};

/** GraphEditor's live set of macros for the current patch — owned and serialised exactly like
 *  synth::PatchDocument and Timeline/TimelineDoc: pure in-memory state. toVar()/fromVar() are
 *  driven by whoever owns the project.json round trip (ProjectBundle); this class never
 *  interprets the data itself, and never touches the graph. */
class MacroSet {
public:
    void clear() { macros_.clear(); }
    bool empty() const { return macros_.empty(); }
    int size() const { return (int)macros_.size(); }

    const std::vector<Macro>& getAll() const { return macros_; }

    Macro* find(const juce::String& macroId);
    const Macro* find(const juce::String& macroId) const;

    /** The macro `memberUuid` belongs to, or nullptr if it is in none (flat model — at most
     *  one macro can ever claim a given member). */
    Macro* findByMember(const juce::String& memberUuid);
    const Macro* findByMember(const juce::String& memberUuid) const;

    /** Adds a new macro (an empty `id` is assigned a fresh one) and returns a reference to the
     *  stored copy — stable until the next mutating call. */
    Macro& add(Macro macro);

    /** Removes the macro. Does NOT touch its former members' graph nodes — ungrouping is purely
     *  a metadata change. */
    bool remove(const juce::String& macroId);

    /** Removes `memberUuid` from whichever macro contains it (no-op if it is in none). A macro
     *  left with zero members afterwards is dissolved outright — an empty macro is not a
     *  meaningful state to leave on screen.
     *  @return the id of the macro that was touched (dissolved or just shrunk), or an empty
     *          string if `memberUuid` was in no macro. */
    juce::String removeMemberEverywhere(const juce::String& memberUuid);

    /** Drops every member uuid that isn't in `aliveMemberUuids`, dissolving any macro left with
     *  no members. Call after any graph mutation that can remove nodes — the MacroSet analogue
     *  of SelectionModel::retainOnly / TimelineReconciler::reconcile.
     *  @return true if anything was dropped or dissolved. */
    bool retainOnly(const std::vector<juce::String>& aliveMemberUuids);

    juce::var toVar() const;

    /** All-or-nothing, mirroring TimelineDoc::fromVar: a malformed `state` (wrong types, a
     *  macro with zero members, a duplicate id, a member uuid claimed by more than one macro)
     *  rejects the WHOLE load and leaves `this` completely untouched. Does not check members
     *  against a live graph — call retainOnly() with the graph's current node uuids afterwards,
     *  the same two-step TimelineReconciler::reconcile follows for TimelineDoc. */
    bool fromVar(const juce::var& state);

private:
    std::vector<Macro> macros_;
};

} // namespace synth
