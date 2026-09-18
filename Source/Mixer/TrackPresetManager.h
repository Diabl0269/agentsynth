#pragma once

#include "MacroSet.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <vector>

namespace synth {

/** Which `+ Track` submenu group (and which Preferences -> Mixer dropdown) a saved track preset
 *  belongs to. Written once at save time from the same track-kind resolution the track header
 *  already has (never inferred from node shape at load time — docs/mixer/track-presets.md). A MIDI track
 *  that alone drives an instrument counts as Instrument, per §5.2's link rule. */
enum class TrackPresetKind { Audio, Instrument };

/** One track preset as surfaced in a menu or a Preferences dropdown. */
struct TrackPresetInfo {
    juce::String name;
    juce::File file;
    TrackPresetKind kind = TrackPresetKind::Audio;
    int moduleCount = 0;
};

/**
 * @class TrackPresetManager
 * @brief Save a track's channel (plus every outside module feeding it through a port) as a reusable
 *        named preset, and drop it back in as a new track's starting chain.
 *
 * Modelled 1:1 on SnippetManager (same on-disk shape, same save/load/persistence contract) but a
 * separate class, not a SnippetManager extension: a track preset carries two fields no snippet
 * does (`trackPresetKind`, `channelMacroId`), lives in its own on-disk folder with its own
 * extension so a file picker can tell the two apart, and needs per-type default bookkeeping
 * SnippetManager has no reason to know about. Generic string utilities are still reused directly
 * from SnippetManager rather than forked — see sanitiseName().
 *
 * The JSON shape is the SAME dialect AIStateMapper::graphToJSON / SnippetManager produce (`nodes`,
 * `connections`, `modulations`, optional `macros`), plus:
 *
 *   - `"schemaVersion"`: 1, same "absent means 1" rule as AIStateMapper::kSchemaVersion.
 *   - `"trackPresetKind"`: `"audio"` or `"instrument"` — which `+ Track` submenu/Preferences
 *     dropdown this belongs to.
 *   - `"channelMacroId"`: the snippet-local id (see "nodes") of this macro's own Channel Strip
 *     member, written defensively at extraction time so the file names which of its (usually one)
 *     captured macros is the channel; the load path does not need to consume it, since
 *     synth::isChannelMacro can re-derive the same answer from the live graph after insert, the
 *     same way the macro's own right-click menu does. Absent only if extraction somehow captured
 *     a macro with no Channel Strip member at all (defensive).
 *
 * Everything here is static and free of GUI dependencies, so it is testable headlessly.
 */
class TrackPresetManager {
public:
    using NodeID = juce::AudioProcessorGraph::NodeID;

    /** On-disk extension — deliberately NOT SnippetManager::kFileExtension, so a file picker (or
     *  TrackPresetManager::loadTrackPreset itself) can never confuse the two formats. */
    static constexpr const char* kFileExtension = ".agtrackpreset";

    /** The literal name reserved for the Preferences dropdown's "use the factory chain" sentinel
     *  (id 0) — sanitiseName() below refuses it so a saved preset can never shadow that entry. */
    static constexpr const char* kFactoryDefaultSentinel = "Factory Default";

    // ---- Storage location -------------------------------------------------------------

    /** `<userAppData>/<settingsFolder>/TrackPresets`, created on demand — exact mirror of
     *  SnippetManager::getDefaultSnippetsDirectory(). */
    static juce::File getDefaultTrackPresetsDirectory();

    // ---- Pure JSON transforms (no filesystem, no graph mutation) ----------------------

    /**
     * Captures `channelMacroId`'s own members (the "own box") PLUS every outside module that
     * feeds it through a port (docs/mixer/track-presets.md#what-a-saved-preset-carries-beyond-the-box's founder requirement,
     * collectOutsideModulatorsForTrackPreset), as one preset var.
     *
     * `includeExtraState` is always forced on internally — a track preset must always carry
     * strip shape/gain/pan (ChannelStripModule's extra state) or a Mono strip would silently
     * reload as the Stereo default. `"solo"` is scrubbed from every captured Channel Strip's
     * state regardless (root CLAUDE.md tripwire: an imported `soloed_=true` would silence the
     * whole mix render-wide).
     *
     * @return a void var when `channelMacroId` doesn't resolve in `macros`, or resolves to a
     *         macro with no live members left.
     */
    static juce::var extractTrackPreset(juce::AudioProcessorGraph& graph, const MacroSet& macros,
                                        const juce::String& channelMacroId, TrackPresetKind kind,
                                        const juce::String& name);

    /** `"trackPresetKind"` read back off a loaded/extracted preset var; `TrackPresetKind::Audio`
     *  when absent or unrecognised. */
    static TrackPresetKind getPresetKind(const juce::var& preset);

    // ---- Graph mutation --------------------------------------------------------------

    /**
     * Thin wrapper over SnippetManager::insertSnippet — THIS is the SnippetManager::insertSnippet /
     * ProjectBundle::load pairing docs/mixer/track-presets.md and the root CLAUDE.md both name: strict
     * (untrusted) validatePatch first, then a trusted, exact-subgraph applyJSONToGraph, ids
     * renumbered via nextFreeIdBase.
     *
     * `includeExtraState=true` is a DELIBERATE, PERMANENT difference from every other
     * insertSnippet caller (besides the in-memory clipboard) — see extractTrackPreset's own
     * comment for why (strip shape/gain/pan must survive; solo is scrubbed separately, at
     * extraction time, so it never reaches this call in the first place).
     *
     * @return the node ids added (Attenuverters excluded), empty on rejection/failure — same
     *         contract as SnippetManager::insertSnippet.
     */
    static std::vector<NodeID> insertTrackPreset(const juce::var& preset, juce::AudioProcessorGraph& graph,
                                                 juce::Point<int> dropPos, std::vector<Macro>* outMacros = nullptr);

    // ---- Persistence -----------------------------------------------------------------

    /** Trims/length-caps/strips-hostile-characters exactly like SnippetManager::sanitiseName
     *  (reused directly, not forked — it has no snippet-specific logic), PLUS refuses the literal
     *  "Factory Default" sentinel (case-insensitive) so a saved preset can never shadow the
     *  Preferences dropdown's "use the factory chain" entry. Empty return means reject, same
     *  contract as SnippetManager::sanitiseName. */
    static juce::String sanitiseName(const juce::String& raw);

    /** `dir/<sanitised name>.agtrackpreset`. Empty file when the name sanitises to nothing. */
    static juce::File fileForName(const juce::File& dir, const juce::String& name);

    static bool saveTrackPreset(const juce::File& dir, const juce::String& name, const juce::var& preset);

    /** Parsed preset, or a void var when the name doesn't resolve to a readable file. */
    static juce::var loadTrackPreset(const juce::File& dir, const juce::String& name);

    /** Parsed preset from an arbitrary file (the "Insert Track Preset from File..." chooser, which
     *  is not confined to getDefaultTrackPresetsDirectory()) — a void var when unreadable/not an
     *  object, same contract as loadTrackPreset. */
    static juce::var loadTrackPresetFile(const juce::File& file);

    /** Every readable `kind` preset in `dir`, sorted by name (case-insensitive). */
    static juce::Array<TrackPresetInfo> listTrackPresets(const juce::File& dir, TrackPresetKind kind);

    static bool deleteTrackPreset(const juce::File& dir, const juce::String& name);
};

} // namespace synth
