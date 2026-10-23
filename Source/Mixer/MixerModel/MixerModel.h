#pragma once

// MixerModel.h (docs/mixer/panel.md): the mixer panel's own Core query
// layer. Headless, no juce_gui_basics/AppUI dependency (Source/Mixer/CLAUDE.md's own "Core,
// no-UI-dep" discipline, same as Source/Mixer/ChannelFlows): everything the mixer panel PAINTS is
// a pure read off the live graph/TimelineDoc/MacroSet, recomputed on demand -- exactly the same
// "never cache, the graph can change under you" reasoning ChannelFlowsTrackChannelLink.h states for
// its own link query.

#include "MacroSet.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <algorithm>
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace synth {

/** One row of a column's insert list, in signal order (docs/mixer/mixer.md#parallel-paths). A split chain lists
 *  each split as a Split row followed by its paths' rows, one level deeper, path by path; a path wired straight to
 *  the merge, with no module, is one DryPath row. */
struct MixerInsertEntry {
    enum class Kind { Module, Split, DryPath };

    /** Invalid for a Split row: it stands for the fork, not a node of its own. */
    juce::AudioProcessorGraph::NodeID nodeId;
    /** A Split or DryPath row carries the uuid of the node that fans out (the track source or an insert). */
    juce::String uuid;
    juce::String name;
    bool bypassed = false;
    Kind kind = Kind::Module;
    /** 0 on the chain itself, +1 inside each split. */
    int depth = 0;
    /** Which path of the enclosing split this row sits on (0-based); -1 at depth 0. */
    int path = -1;
    /** Split rows only: how many paths the split has. */
    int pathCount = 0;

    bool operator==(const MixerInsertEntry&) const = default;
};

/** How many modules `inserts` lists (its Split rows are not modules). */
inline int countInsertModules(const std::vector<MixerInsertEntry>& inserts) {
    return (int)std::count_if(inserts.begin(), inserts.end(),
                              [](const MixerInsertEntry& e) { return e.kind == MixerInsertEntry::Kind::Module; });
}

/** One of a source column's active send slots, in slot order (docs/mixer/sends-and-buses.md). The slot index is
 * the identity -- it names the jack, the row, and the `sendNLevel` parameter alike -- and the target is read off the
 * graph every rebuild, never stored. */
struct MixerSendEntry {
    int slot = 0;
    bool preFader = false;
    /** Silenced independent of its level (ChannelStripModule::isSendMuted). */
    bool muted = false;
    /** Ramped to silence without touching its level (ChannelStripModule::isSendBypassed). */
    bool bypassed = false;
    /** Summed to (L+R)*0.5 before its own pan law (ChannelStripModule::isSendMono). */
    bool mono = false;
    /** The bus this slot feeds. Invalid when the slot's cable has been cut on the canvas, in which
     *  case `targetName` is the "no target" placeholder. */
    juce::AudioProcessorGraph::NodeID targetNodeId;
    /** `targetNodeId` is a module whose Key input this send feeds, not a strip. */
    bool keyTarget = false;
    juce::String targetName;

    bool operator==(const MixerSendEntry&) const = default;
};

/** One column of the mixer panel: a ChannelStrip, a group/send bus (also a ChannelStrip --
 *  docs/mixer/sends-and-buses.md#a-bus-is-a-channel-strip: there is no separate bus node type), the always-present
 * Direct bus once Master exists, or Master itself. */
struct MixerColumn {
    enum class Kind { Strip, Bus, Direct, Master };

    Kind kind = Kind::Strip;

    /** Invalid (default-constructed) for Direct -- it has no node of its own. */
    juce::AudioProcessorGraph::NodeID nodeId;
    juce::String uuid;
    juce::String name;
    juce::Colour colour{0xff5a7dff};

    /** Strip's ONLY feeding source (docs/mixer/mixer.md#channels-follow-audio-not-tracks). None for Direct/Master. */
    bool linkedToTrack = false;

    /** Every track whose header shows this column's chip (docs/mixer/mixer.md#channels-follow-audio-not-tracks). Empty
     * for an orphan strip nothing in the timeline feeds, and for Direct/Master. */
    std::vector<TrackId> feedingTracks;

    /** The feeding track's own source node -- the insert chain's implicit predecessor (needed to
     *  splice a new FIRST insert in ahead of an empty/one-entry chain). Master's own node for Kind::Master;
     *  invalid for Direct, for a non-bus orphan strip, and for a Kind::Bus column (a bus's own chain has no
     *  external source -- nothing feeds its EQ from outside -- so MixerInsertList::moveRow refuses
     *  to move a row to the very front of a bus's chain rather than splice against an invalid id). */
    juce::AudioProcessorGraph::NodeID sourceNodeId;

    /** The chain between this strip and its own upstream source, in signal order
     * (docs/mixer/mixer.md#inserts-in-a-free-form-graph): the feeding track's source for an ordinary strip, or
     * (docs/mixer/sends-and-buses.md) the strip's own EQ/Compressor chain walked BACKWARD for a Kind::Bus column, which
     * has no feeding track to walk forward from. A send feeding the bus (landing on the same strip input channels an
     *  insert's own output would) is excluded from this walk, never counted as a bus insert or as
     *  branching. Master's is the forward walk to `chainEndNodeId`. Empty for Direct and for a non-bus orphan
     *  strip. */
    std::vector<MixerInsertEntry> inserts;

    /** Kind::Master only: the Rec Tap / Audio Output the chain feeds -- its terminator, never an insert. */
    juce::AudioProcessorGraph::NodeID chainEndNodeId;

    /** False => `inserts` is read-only (a branch, a shared node, or -- for a bus -- more than one
     *  send/insert landing on the same strip input sits upstream); the column shows "Edit on
     *  canvas" instead of add/reorder/remove. Meaningless when `inserts` is empty. */
    bool insertChainIsLinear = false;

    /** True when the chain splits into parallel paths that all meet again before the strip
     *  (docs/mixer/mixer.md#parallel-paths): `inserts` then holds Split rows, insertChainIsLinear stays false (so a
     *  track delete keeps the modules, as for any branch), and the list stays editable row by row. */
    bool insertChainHasSplits = false;

    /** This strip's active send slots, in slot order (docs/mixer/sends-and-buses.md). Empty for Direct/Master and for
     * any strip with no sends. */
    std::vector<MixerSendEntry> sends;

    /** Kind::Bus only: the names of the strips feeding this bus, in ascending NodeID -- what a bus
     *  column shows on its source line instead of feeding-track names. */
    std::vector<juce::String> busSources;

    /** Kind::Strip only: strips sending into this track channel (docs/mixer/sends-and-buses.md). */
    std::vector<juce::String> receivesFrom;

    /** Set only when `insertChainIsLinear` is false: the owning macro's id when the branching/
     *  shared node is boxed, else that node's own uuid. Empty otherwise. */
    juce::String editOnCanvasTargetUuid;

    /** Every field equal: a column the panel already shows for an equal column is kept rather than rebuilt. */
    bool operator==(const MixerColumn&) const = default;
};

struct MixerSnapshot {
    std::vector<MixerColumn> columns;
    bool hasDirect = false;
    bool hasMaster = false;
};

/** Builds the full column set (docs/mixer/panel.md#what-the-mixer-shows: strips in track order, then buses, then
 * Direct, then Master -- nothing else). Recomputed on demand; cheap enough to call on every graph/timeline/ macro
 * change notification (a handful of strips, never per-frame). */
MixerSnapshot buildMixerSnapshot(juce::AudioProcessorGraph& graph, const TimelineDoc& doc, const MacroSet& macros);

// ---- Insert-list mutations (docs/mixer/mixer.md#inserts-in-a-free-form-graph) --------------
//
// Plain graph splices, NO UNDO of their own -- same contract as Source/Mixer/ChannelFlows's own
// builders (docs/architecture/module-base.md#appundomanager): the caller wraps each in one
// AppUndoManager::recordGraphAndMacroChange. Only ever call these on a column whose own
// insertChainIsLinear or insertChainHasSplits is true; behaviour on a branching chain's member is undefined (the UI
// never offers add/reorder/remove there in the first place).
//
// spliceOutInsert bridges by matching `node`'s OWN input channel index to its OWN output channel
// index -- true for every ordinary passthrough FX module (an input jack and the output jack
// carrying the same leg always share one channel number, the same invariant the bypass/mute
// contract relies on). spliceInInsert then re-targets each spliced connection at `node`'s own L/R
// channel numbering (left always ch0, right whatever `node` itself reports as its right leg --
// Source/Modules/CLAUDE.md) rather than reusing the old destination's numbers verbatim, since two
// different module types (an ordinary FX module vs. e.g. ChannelStripModule's kRightBase) need not
// agree on which channel is "right".

/** Every primitive below takes `stripEnd` (the column's strip) to ignore dangling branches; invalid counts every edge.
 *  Detaches `node` from its chain, bridging its single signal predecessor directly to its single
 *  signal successor. Does NOT remove `node` from the graph -- Remove calls
 *  `juce::AudioProcessorGraph::removeNode()` afterward; Reorder immediately re-splices `node`
 *  elsewhere with spliceInInsert. False (nothing changed) when `node` does not have exactly one
 *  signal predecessor and one signal successor. */
bool spliceOutInsert(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID node,
                     juce::AudioProcessorGraph::NodeID stripEnd = {});

/** Splices `node` (already in the graph, not yet wired into this chain) between `predecessor` and
 *  `successor`'s existing direct signal connection(s), removing that connection and reconnecting
 *  predecessor -> node -> successor channel-for-channel. False when predecessor and successor have
 *  no direct signal connection to splice into. */
bool spliceInInsert(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID predecessor,
                    juce::AudioProcessorGraph::NodeID successor, juce::AudioProcessorGraph::NodeID node);

/** Moves an already-connected `node` to a new position between `newPredecessor` and
 *  `newSuccessor`: spliceOutInsert() then spliceInInsert() as one call, so a caller never has to
 *  get the two-step sequence right itself. False if either step fails (graph left as spliceOutInsert
 *  found it in that case -- see spliceOutInsert's own contract). */
bool reorderInsert(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID node,
                   juce::AudioProcessorGraph::NodeID newPredecessor, juce::AudioProcessorGraph::NodeID newSuccessor,
                   juce::AudioProcessorGraph::NodeID stripEnd = {});

/** `node`'s one signal neighbour upstream (`upstream`) or downstream, counting a stereo pair as one; an invalid id
 *  when it has none or several. */
juce::AudioProcessorGraph::NodeID soleSignalNeighbour(juce::AudioProcessorGraph& graph,
                                                      juce::AudioProcessorGraph::NodeID node, bool upstream,
                                                      juce::AudioProcessorGraph::NodeID stripEnd = {});

/** On a path of a split chain (docs/mixer/mixer.md#parallel-paths): moves `node` to sit just before `before`, its
 *  neighbour on the same path, between `before` and `before`'s own single signal predecessor. False (nothing changed)
 *  when `before` has no single predecessor. */
bool moveInsertBefore(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID node,
                      juce::AudioProcessorGraph::NodeID before, juce::AudioProcessorGraph::NodeID stripEnd = {});

/** The mirror of moveInsertBefore: moves `node` to sit just after `after`, between it and its single successor. */
bool moveInsertAfter(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID node,
                     juce::AudioProcessorGraph::NodeID after, juce::AudioProcessorGraph::NodeID stripEnd = {});

} // namespace synth
