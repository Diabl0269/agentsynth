// GraphEditorTypes.h
//
// Nested value types GraphEditor exposed as GraphEditor::X, moved out to their own header (FRO77
// PR3 header-trim, rule 4) purely to shrink GraphEditor.h — no behavior change. GraphEditor.h
// pulls this in and re-exposes each type via a `using` alias, so every existing `GraphEditor::X`
// spelling (callers, tests) keeps compiling unchanged.
#pragma once

#include "UI/Graph/CableColour.h"
#include <cstdint>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace graph_editor_types {

// ---- Locate Master (FRO45) -----------------------------------------------------------------
// Founder feedback on T183's live check: once Master and Audio Output exist (T187 seeds an Audio
// Output on New Patch), auto-arrange or a drag can leave them anywhere on the canvas. This is the
// lightweight canvas-only stopgap — the durable answer is the future mixer panel (P9-5,
// docs/mixer/mixer.md), not built here.
enum class LocateMasterResult {
    NoTarget,   // neither node exists yet — graceful no-op
    Master,     // Master was selected and brought into view
    AudioOutput // no Master yet; fell back to Audio Output
};

/** Raw-channel wiring for a single cable: the channel each end starts at, and how many
 *  consecutive per-voice channels the cable covers (1 = an ordinary mono wire).
 *  Voice v connects sourceRawChannel + v * sourceStride -> destRawChannel + v. */
struct PolyLink {
    int sourceRawChannel = 0;
    int destRawChannel = 0;
    int voiceCount = 1;
    // 1 for a voice-to-voice fan; 0 when a single mono source is broadcast to every voice of
    // the destination fan, so all N wires leave the same source channel.
    int sourceStride = 1;
};

// ---- Cables (issue #157) --------------------------------------------------------------------
// A "cable" is one wire as the USER sees it, which is not the same thing as a graph edge: an
// attenuverter chain is two edges plus a hidden node, and a poly bus is N edges. Both render as a
// single wire, so anything that identifies, hit-tests, or colours a cable has to key on this
// logical view rather than on juce::AudioProcessorGraph::Connection.

/** Stable identity for a user-visible cable. Survives repaints; used to tell whether the
 *  hovered cable actually changed (so hover does not repaint on every mouse move). */
struct CableId {
    uint32_t srcUid = 0;
    int srcPort = 0;
    uint32_t dstUid = 0;
    int dstPort = 0;
    uint32_t attenUid = 0; // non-zero only for an attenuverter chain

    bool operator==(const CableId& o) const noexcept {
        return srcUid == o.srcUid && srcPort == o.srcPort && dstUid == o.dstUid && dstPort == o.dstPort &&
               attenUid == o.attenUid;
    }
    bool operator!=(const CableId& o) const noexcept { return !(*this == o); }
};

/** One drawn wire, with everything paint() and hit-testing need. Produced by
 *  buildVisibleCables() so the canvas and the mouse agree on where cables are — if these
 *  were computed separately they would drift and clicks would miss the wire. */
struct VisibleCable {
    enum class Kind {
        Direct,            // a plain audio or MIDI graph edge
        AttenuverterChain, // source -> attenuverter -> destination, drawn as one wire + knob
        ModRouting         // DirectCV / PolyBus mod routing
    };

    CableId id;
    Kind kind = Kind::Direct;
    juce::Point<float> p1, p2; // canvas coords
    synth::ui::CableSignal signal = synth::ui::CableSignal::Audio;
    synth::ui::ModuleCategory sourceCategory = synth::ui::ModuleCategory::Utility;
    bool isBypassed = false;
    float activity = 0.0f;    // drives brightness / width
    bool isPolyBus = false;   // RoutingKind::PolyBus — drives the "xN" bundle badge
    int voiceCount = 1;       // PolyBus bundle size (badge)
    float attenAmount = 0.0f; // AttenuverterChain knob value, -1..1
};

} // namespace graph_editor_types
