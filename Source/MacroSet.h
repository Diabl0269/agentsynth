#pragma once

#include <algorithm>
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <vector>

namespace synth {

/** Whether a MacroPort carries an audio/CV signal (a channel shape — Mono/Stereo/Poly-N, see
 *  MacroInletModule) or MIDI (no shape at all, just note/CC events — see MacroMidiInletModule).
 *  Determines which pair of node types (MacroInlet/MacroOutlet vs MacroMidiInlet/MacroMidiOutlet)
 *  the port-creation flow constructs for this port; kept on the port itself too so a macro's card
 *  can be drawn from its port list alone, with no per-port graph lookup needed to tell audio/CV
 *  and MIDI jacks apart. */
enum class MacroPortKind { AudioCV, Midi };

/** One inlet or outlet jack on a Macro's boundary (P8-15 Macro I/O; docs/macros.md §5). The node
 *  named by `nodeUuid` (a MacroInlet/MacroOutlet or MacroMidiInlet/MacroMidiOutlet — always also
 *  a member of the same Macro, exactly like any other node) is the ground truth for what actually
 *  carries signal; this struct is the macro-level PRESENTATION layered over it — name and draw
 *  order — the same relationship a Macro's own name/colour/bounds have to its members. */
struct MacroPort {
    juce::String nodeUuid; // the MacroInlet/MacroOutlet (or MIDI variant) node this port fronts
    bool isInput = false;  // true: signal flows INTO the macro (an inlet); false: an outlet
    juce::String name;     // user-visible port name ("Pitch In", "Wet Out")
    int order = 0;         // draw order on the card, user-reorderable
    MacroPortKind kind = MacroPortKind::AudioCV;

    juce::var toVar() const;

    /** All-or-nothing, mirroring Macro/MacroSet's own fromVar: a malformed entry (not an object,
     *  an empty nodeUuid, an unrecognised "kind") leaves `out` untouched and returns false, so a
     *  caller parsing a whole macro's port list can reject it as one unit rather than accept a
     *  partially-parsed port. */
    static bool fromVar(const juce::var& v, MacroPort& out);
};

/** A named, coloured, collapsible grouping of graph nodes on the canvas — presentation over a
 *  flat graph (P8-12; docs/layout_selection_canvas.md), now gaining optional named ports
 *  (P8-15 Macro I/O, docs/macros.md §5). A Macro still adds no graph edges and no processing of
 *  its own: `ports` is a description of which of its OWN inlet/outlet member nodes are exposed as
 *  named jacks, not a mechanism the macro itself implements — the boundary stays a rendering
 *  concept (docs/macros.md §5.4).
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

    // Named jacks on this macro's boundary (P8-15). Every port's nodeUuid MUST also appear in
    // `members` — an inlet/outlet is a member like any other node (docs/macros.md §5.1) — and
    // MacroSet::fromVar rejects a saved macro where that does not hold. Order in this vector is
    // NOT the draw order; use each port's own `order` field (user-reorderable, §7 item 5).
    std::vector<MacroPort> ports;

    bool hasMember(const juce::String& memberUuid) const {
        return std::find(members.begin(), members.end(), memberUuid) != members.end();
    }

    /** True if `memberUuid` fronts one of this macro's ports (a MacroInlet/MacroOutlet or MIDI
     *  variant node) rather than being an ordinary module the user grouped. Presentation-only —
     *  it answers "is this member a boundary jack", nothing more; `members` itself, and every
     *  consumer that reads it for bounds/group-drag/bypass-mute/undo/serialization, is completely
     *  untouched by this (founder-review fix G6, docs/macros.md §7 item 4 note). */
    bool memberIsPort(const juce::String& memberUuid) const {
        return std::any_of(ports.begin(), ports.end(), [&](const MacroPort& p) { return p.nodeUuid == memberUuid; });
    }

    /** The user-facing MODULE count: `members.size()` minus the members that are actually port
     *  nodes (founder-review fix G6). Grouping N modules with a crossing cable can splice in
     *  inlet/outlet nodes that are genuine members (they must be, for bounds/drag/undo to work —
     *  see the class comment above and docs/macros.md §5.1), but a port is a boundary jack the
     *  macro exposes, not a module the user put in the box — those are different quantities, and
     *  every user-facing count/list must report the first one, not `members.size()`. This is the
     *  ONE place that filter lives; route every presentation call site through it (or through
     *  memberIsPort() for a list) rather than re-deriving the same exclusion. */
    int moduleMemberCount() const {
        return (int)std::count_if(members.begin(), members.end(),
                                  [&](const juce::String& uuid) { return !memberIsPort(uuid); });
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

    /** Removes the macro. Does NOT touch its former members' graph nodes itself — this call is
     *  purely a metadata change, same as every other MacroSet mutator. GraphEditor::
     *  ungroupSelection() (founder-review fix G7, docs/macros.md §7 item 8) is the caller that
     *  gives "ungroup" its full user-facing meaning: it splices every one of the macro's PORT
     *  nodes back out (GraphEditor::spliceOutMacroPort) — removing them and reconnecting the
     *  cable each proxied — before ever calling this, so by the time `remove()` runs, only
     *  ordinary member nodes (untouched, exactly as this method's own contract says) are left. */
    bool remove(const juce::String& macroId);

    /** Removes `memberUuid` from whichever macro contains it (no-op if it is in none), also
     *  dropping any port that fronted it (P8-15) — a port's nodeUuid is always a member, so this
     *  keeps that invariant true after a single-member removal too, not only after retainOnly().
     *  A macro left with zero members afterwards is dissolved outright — an empty macro is not a
     *  meaningful state to leave on screen.
     *  @return the id of the macro that was touched (dissolved or just shrunk), or an empty
     *          string if `memberUuid` was in no macro. */
    juce::String removeMemberEverywhere(const juce::String& memberUuid);

    /** Drops every member uuid that isn't in `aliveMemberUuids`, and with it any port (P8-15)
     *  whose node died — a port whose node is gone is not a representable state, the same way a
     *  macro with zero members isn't. Dissolves any macro left with no members. Call after any
     *  graph mutation that can remove nodes — the MacroSet analogue of SelectionModel::retainOnly
     *  / TimelineReconciler::reconcile.
     *  @return true if anything was dropped or dissolved. */
    bool retainOnly(const std::vector<juce::String>& aliveMemberUuids);

    juce::var toVar() const;

    /** All-or-nothing, mirroring TimelineDoc::fromVar: a malformed `state` (wrong types, a
     *  macro with zero members, a duplicate id, a member uuid claimed by more than one macro, a
     *  malformed port, or a port whose nodeUuid is not one of the macro's own members) rejects
     *  the WHOLE load and leaves `this` completely untouched. An absent "ports" key on a macro
     *  parses as no ports — every P8-12 project.json predates the key. Does not check members (or
     *  ports) against a live graph — call retainOnly() with the graph's current node uuids
     *  afterwards, the same two-step TimelineReconciler::reconcile follows for TimelineDoc. */
    bool fromVar(const juce::var& state);

private:
    std::vector<Macro> macros_;
};

} // namespace synth
