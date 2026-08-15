#pragma once

#include "../Transport/TransportService.h"
#include "TimelineDoc.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

namespace synth {

/**
 * @brief Compact, token-bounded, read-only summary of the arrangement for AI prompt context —
 *        the timeline sibling of the patch-context injection
 *        (`AIIntegrationService::buildPatchAugmentedContent`, Source/AI/AIIntegrationService.cpp).
 *
 * ### Security model (TL8 — see TimelineValidator.h and docs/AI_Engine.md §5c)
 *
 * `summarize()` is a READ path only: it never mutates the doc, the graph or the transport
 * snapshot, and nothing it emits round-trips back into the timeline (there is no "un-summarize").
 * Two rules keep it from handing a model more than a sound designer would type into the chat box
 * themselves:
 *
 *  - **Never a file path.** An audio clip's `assetRef` is bundle-relative ("Audio/take-1.wav" —
 *    see `Clip::assetRef`); only the bare file name (everything after the last `/`) reaches the
 *    summary. The directory component — and with it anything about the bundle's on-disk layout or
 *    a user's own take-naming scheme — is deliberately dropped, never merely stripped-then-put-back.
 *  - **Never a plugin/implementation identifier.** A bound node is named by its display name (the
 *    same string its module title bar shows, `juce::AudioProcessor::getName()`) — never a node id,
 *    factory type key, or uuid. A binding that does not resolve against the live graph reports
 *    "MISSING" rather than leaking the raw uuid it failed to resolve.
 *
 * ### Format
 *
 * Compact structured text, one line per item, in `TimelineDoc`'s own stable order (tracks as
 * stored; clips within a track already kept sorted by `startBeat`). See the .cpp for the exact
 * grammar. `maxChars` is respected by truncating whole TRACKS from the tail — never mid-line —
 * and appending a deterministic "… [+K more tracks]" marker when anything was dropped.
 */
struct ArrangementContext {
    /**
     * @param doc       the timeline document to summarise. `doc.isEmpty()` => `""`.
     * @param graph     the LIVE graph, used only to resolve a bound node's uuid to its display
     *                  name. A track/lane binding whose uuid does not match any live node's
     *                  "uuid" property is reported as "MISSING", regardless of what `doc`'s own
     *                  (possibly stale) `orphaned` flags say — this function always resolves
     *                  against `graph` directly rather than trusting cached reconciliation state.
     * @param transport bpm / time signature / loop, read from `TransportService::getPositionSnapshot()`.
     * @param maxChars  soft cap on the returned string's length. The header and every fully
     *                  included track are never split mid-line; the tail-truncation marker itself
     *                  is exempt from the cap (it is what explains the truncation).
     */
    static juce::String summarize(const TimelineDoc& doc, const juce::AudioProcessorGraph& graph,
                                  const TransportService::PositionSnapshot& transport, int maxChars = 2000);
};

} // namespace synth
