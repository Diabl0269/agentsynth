#pragma once

#include "Mixer/TrackChannelLink.h"
#include "TrackChannelLinkSurface.h"
#include <cstdint>
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>

// TrackChannelLinkController.h -- FRO14 (P9-4, docs/mixer.md §5.2): the app-side half of the track
// <-> channel link. The rule itself is Core (Source/Mixer/TrackChannelLink.h, a pure query); this
// class is what ACTS on it -- renaming both sides, fanning a live colour preview out over track and
// macro, driving a linked channel's mute/solo, and revealing a channel in the graph.
//
// A collaborator owned by MainComponent rather than more methods on MainComponent itself (the same
// seam pattern GraphCanvasHost/MacroGroupController established): MainComponent gains exactly one
// member and one one-line override, and everything else -- including all of this contract -- lives
// here.
//
// UNDO CONTRACT, per surface:
//   - name  : ONE compound graph+timeline+macro step, both directions (see renameLinkedTrackAndChannel
//             and installMacroRenameHook).
//   - colour: preview writes with NO undo step on every frame; commit is ONE compound step; a
//             no-net-change close restores both targets and pushes nothing.
//   - M/S   : ONE graph snapshot step each (captureBeforeState/pushSnapshotFromCapture, the
//             ModuleComponent idiom). The strip's mute is a real parameter and its solo is trusted
//             extra state, so both round-trip through the snapshot; the restore hook's publish is
//             what settles the engine's solo count afterwards.
//   - link formation (reconcileLinkedTracks): NOT undoable, deliberately -- the same rule
//             MainComponent's orphan-flag reconciliation already states.

class AudioEngine;
class AppUndoManager;
class GraphEditor;
class ChannelStripModule;

namespace synth {
struct Macro;
class MacroSet;
} // namespace synth

namespace synth::ui {

class TrackChannelLinkController final : public TrackChannelLinkSurface {
public:
    /** Installs the macro-rename hook on `graphEditor`'s MacroGroupController, so renaming a boxed
     *  channel renames its linked track inside the SAME undo transaction. */
    TrackChannelLinkController(AudioEngine& engine, synth::TimelineDoc& doc, AppUndoManager& undo,
                               GraphEditor& graphEditor);
    ~TrackChannelLinkController() override;

    ChannelInfo getChannelInfo(synth::TrackId track) const override;
    float getChannelMeterPeak(synth::TrackId track) const override;
    bool renameLinkedTrackAndChannel(synth::TrackId track, const juce::String& newName) override;
    std::unique_ptr<ColourPickerPopup> buildLinkedChannelColourPicker(synth::TrackId track,
                                                                      juce::PropertiesFile* favourites) override;
    bool toggleLinkedChannelMuted(synth::TrackId track) override;
    bool toggleLinkedChannelSoloed(synth::TrackId track) override;
    void revealChannelForTrack(synth::TrackId track) override;

    /** Runs from MainComponent::reconcileTimelineAfterGraphChange -- the funnel every
     *  graph-structural change and every undo/redo restore already reaches.
     *
     *  §5.2: a linked track stores its mute/solo on the CHANNEL, not on the track, so there is
     *  exactly one of each. When a link forms around a track that was muted/soloed while it still
     *  had note gating, this transfers that state onto the strip and clears the doc flag. Not
     *  undoable, exactly like the orphan-flag reconciliation next to it: it derives runtime state
     *  rather than performing a user edit. A link BREAKING transfers nothing back -- the strip's
     *  mute/solo is now the shared channel's own, and the tracks' doc flags are already false, so
     *  note gating simply resumes for them. */
    void reconcileLinkedTracks();

private:
    juce::AudioProcessorGraph& graph() const;
    synth::MacroSet& macros() const;
    /** The live link state for `track`, recomputed from the graph every call. */
    synth::TrackChannelLinkInfo resolve(synth::TrackId track) const;
    ChannelStripModule* stripFor(const synth::TrackChannelLinkInfo& info) const;
    /** The macro the channel's strip is boxed in, or null for a hand-built (unboxed) chain. */
    const synth::Macro* macroForStrip(const juce::String& stripUuid) const;
    /** The single track linked to the channel `macroId` boxes, invalid when it boxes none. */
    synth::TrackId linkedTrackForMacro(const juce::String& macroId) const;
    void installMacroRenameHook();

    /** TrackId -> the strip that track plays into, as last resolved by getChannelInfo() or
     *  reconcileLinkedTracks(). ONLY the 15 Hz meter read uses it, and only as a hint -- see
     *  TrackChannelLinkSurface::getChannelMeterPeak on why a stale entry cannot mislead. Mutable
     *  because the const display query is what keeps it warm. */
    mutable std::map<std::int64_t, juce::AudioProcessorGraph::NodeID> meterStripIds_;

    AudioEngine& engine_;
    synth::TimelineDoc& doc_;
    AppUndoManager& undo_;
    GraphEditor& graphEditor_;
    // Re-entrancy guard: reconcileLinkedTracks() writes the doc, which notifies, which can land
    // back in the same reconcile funnel.
    bool reconciling_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackChannelLinkController)
};

} // namespace synth::ui
