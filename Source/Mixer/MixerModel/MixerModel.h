#pragma once

// MixerModel.h -- FRO11 (P9-5, docs/mixer.md §5.6/§5.9-5.11): the mixer panel's own Core query
// layer. Headless, no juce_gui_basics/AppUI dependency (Source/Mixer/CLAUDE.md's own "Core,
// no-UI-dep" discipline, same as Source/Mixer/ChannelFlows): everything the mixer panel PAINTS is
// a pure read off the live graph/TimelineDoc/MacroSet, recomputed on demand -- exactly the same
// "never cache, the graph can change under you" reasoning ChannelFlowsTrackChannelLink.h states for
// FRO14's own link query.

#include "MacroSet.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace synth {

/** One module in a column's insert list, in signal order. */
struct MixerInsertEntry {
    juce::AudioProcessorGraph::NodeID nodeId;
    juce::String uuid;
    juce::String name;
    bool bypassed = false;
};

/** One of a source column's active send slots, in slot order (FRO15, §5.15). The slot index is the
 *  identity -- it names the jack, the row, and the `sendNLevel` parameter alike -- and the target is
 *  read off the graph every rebuild, never stored. */
struct MixerSendEntry {
    int slot = 0;
    bool preFader = false;
    /** The bus this slot feeds. Invalid when the slot's cable has been cut on the canvas, in which
     *  case `targetName` is the "no target" placeholder. */
    juce::AudioProcessorGraph::NodeID targetNodeId;
    juce::String targetName;
};

/** One column of the mixer panel: a ChannelStrip, a group/send bus (also a ChannelStrip -- §5.15's
 *  D1: there is no separate bus node type), the always-present Direct bus once Master exists, or
 *  Master itself. */
struct MixerColumn {
    enum class Kind { Strip, Bus, Direct, Master };

    Kind kind = Kind::Strip;

    /** Invalid (default-constructed) for Direct -- it has no node of its own. */
    juce::AudioProcessorGraph::NodeID nodeId;
    juce::String uuid;
    juce::String name;
    juce::Colour colour{0xff5a7dff};

    /** §5.2's link rule: this strip's ONLY feeding track source. Meaningless for Direct/Master. */
    bool linkedToTrack = false;

    /** Every track whose header shows this column's chip (§5.2). Empty for an orphan strip nothing
     *  in the timeline feeds, and for Direct/Master. */
    std::vector<TrackId> feedingTracks;

    /** The feeding track's own source node -- the insert chain's implicit predecessor (needed to
     *  splice a new FIRST insert in ahead of an empty/one-entry chain). Invalid for Direct/Master
     *  and for an orphan strip. */
    juce::AudioProcessorGraph::NodeID sourceNodeId;

    /** The chain between the feeding track's source and this strip, in signal order (§5.6). Empty
     *  for Direct/Master and for an orphan strip with no track to walk from. */
    std::vector<MixerInsertEntry> inserts;

    /** False => `inserts` is read-only (a branch or a shared node sits between source and strip);
     *  the column shows "Edit on canvas" instead of add/reorder/remove. Meaningless when `inserts`
     *  is empty. */
    bool insertChainIsLinear = false;

    /** This strip's active send slots, in slot order (§5.15). Empty for Direct/Master and for any
     *  strip with no sends. */
    std::vector<MixerSendEntry> sends;

    /** Kind::Bus only: the names of the strips feeding this bus, in ascending NodeID -- what a bus
     *  column shows on its source line instead of feeding-track names. */
    std::vector<juce::String> busSources;

    /** Set only when `insertChainIsLinear` is false: the owning macro's id when the branching/
     *  shared node is boxed, else that node's own uuid. Empty otherwise. */
    juce::String editOnCanvasTargetUuid;
};

struct MixerSnapshot {
    std::vector<MixerColumn> columns;
    bool hasDirect = false;
    bool hasMaster = false;
};

/** Builds the full column set (§8 item 4 / §5.10: strips in track order, then buses, then Direct,
 *  then Master -- nothing else). Recomputed on demand; cheap enough to call on every graph/timeline/
 *  macro change notification (a handful of strips, never per-frame). */
MixerSnapshot buildMixerSnapshot(juce::AudioProcessorGraph& graph, const TimelineDoc& doc, const MacroSet& macros);

// ---- Insert-list mutations (§5.6) -------------------------------------------------------------
//
// Plain graph splices, NO UNDO of their own -- same contract as Source/Mixer/ChannelFlows's own
// builders (docs/architecture.md): the caller wraps each in one
// AppUndoManager::recordGraphAndMacroChange. Only ever call these on a column whose own
// insertChainIsLinear is true; behaviour on a branching chain's member is undefined (the UI never
// offers add/reorder/remove there in the first place).
//
// spliceOutInsert bridges by matching `node`'s OWN input channel index to its OWN output channel
// index -- true for every ordinary passthrough FX module (an input jack and the output jack
// carrying the same leg always share one channel number, the same invariant the bypass/mute
// contract relies on). spliceInInsert then re-targets each spliced connection at `node`'s own L/R
// channel numbering (left always ch0, right whatever `node` itself reports as its right leg --
// Source/Modules/CLAUDE.md) rather than reusing the old destination's numbers verbatim, since two
// different module types (an ordinary FX module vs. e.g. ChannelStripModule's kRightBase) need not
// agree on which channel is "right".

/** Detaches `node` from its chain, bridging its single signal predecessor directly to its single
 *  signal successor. Does NOT remove `node` from the graph -- Remove calls
 *  `juce::AudioProcessorGraph::removeNode()` afterward; Reorder immediately re-splices `node`
 *  elsewhere with spliceInInsert. False (nothing changed) when `node` does not have exactly one
 *  signal predecessor and one signal successor. */
bool spliceOutInsert(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID node);

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
                   juce::AudioProcessorGraph::NodeID newPredecessor, juce::AudioProcessorGraph::NodeID newSuccessor);

} // namespace synth
