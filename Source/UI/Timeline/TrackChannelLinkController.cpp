#include "TrackChannelLinkController.h"

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "MacroSet.h"
#include "Mixer/PeakMeterLatch.h"
#include "Modules/ChannelStripModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include <algorithm>

namespace synth::ui {

namespace {
// The chip's last-resort label, when the strip is neither boxed in a macro nor fed by exactly one
// named track (docs/mixer.md §5.2: a shared channel keeps its own independently-chosen name).
constexpr const char* kUnnamedChannel = "Channel";
} // namespace

TrackChannelLinkController::TrackChannelLinkController(AudioEngine& engine, synth::TimelineDoc& doc,
                                                       AppUndoManager& undo, GraphEditor& graphEditor)
    : engine_(engine)
    , doc_(doc)
    , undo_(undo)
    , graphEditor_(graphEditor) {
    installMacroRenameHook();
}

TrackChannelLinkController::~TrackChannelLinkController() {
    // The controller is destroyed with MainComponent, which owns the GraphEditor too -- but member
    // teardown order is not this class' to assume, so the hook is dropped explicitly rather than
    // left holding a `this` that is going away.
    graphEditor_.getMacroController().recordMacroRenameHook = nullptr;
}

juce::AudioProcessorGraph& TrackChannelLinkController::graph() const { return engine_.getGraph(); }

synth::MacroSet& TrackChannelLinkController::macros() const { return graphEditor_.getMacros(); }

synth::TrackChannelLinkInfo TrackChannelLinkController::resolve(synth::TrackId track) const {
    return synth::resolveTrackChannelLink(graph(), doc_, track);
}

ChannelStripModule* TrackChannelLinkController::stripFor(const synth::TrackChannelLinkInfo& info) const {
    auto* node = info.hasChannel ? graph().getNodeForId(info.stripId) : nullptr;
    return node != nullptr ? dynamic_cast<ChannelStripModule*>(node->getProcessor()) : nullptr;
}

const synth::Macro* TrackChannelLinkController::macroForStrip(const juce::String& stripUuid) const {
    return stripUuid.isNotEmpty() ? macros().findByMember(stripUuid) : nullptr;
}

synth::TrackId TrackChannelLinkController::linkedTrackForMacro(const juce::String& macroId) const {
    const auto* macro = macros().find(macroId);
    if (macro == nullptr)
        return {};
    for (const auto& track : doc_.getTracks()) {
        const auto info = resolve(track.id);
        if (info.linked && macro->hasMember(info.stripUuid))
            return track.id;
    }
    return {};
}

// ---- Display ------------------------------------------------------------------------------

TrackChannelLinkSurface::ChannelInfo TrackChannelLinkController::getChannelInfo(synth::TrackId track) const {
    ChannelInfo out;
    const auto info = resolve(track);
    if (!info.hasChannel)
        return out;

    out.hasChannel = true;
    out.linked = info.linked;
    meterStripIds_[track.value] = info.stripId; // keeps the 15 Hz meter read off the graph walk
    // A channel's name IS its macro's name when it is boxed (the macro is the container, §5.2);
    // otherwise the shared Core rule -- the one feeding track's name, else the fallback.
    const auto* macro = macroForStrip(info.stripUuid);
    out.channelName = macro != nullptr && macro->name.isNotEmpty()
                          ? macro->name
                          : synth::channelDisplayName(graph(), info.stripId, doc_, kUnnamedChannel);

    if (auto* strip = stripFor(info)) {
        out.channelMuted = strip->hasMuteParameter() && strip->isMuted();
        out.channelSoloed = strip->isSoloed();
        // FRO146: deliberately NOT populated from strip->takeMeterPeak() here. The strip's
        // TrackHeader latch slot is consume-on-read (PeakMeterLatch.h), and the shared 15 Hz tick
        // (getChannelMeterPeak() below) is that slot's ONE reader -- this call runs on a completely
        // different cadence (a doc/graph change, not a per-frame poll), so reading it here would
        // silently steal a peak the tick path was about to report. `meterPeak` stays at its default
        // (0.0f); nothing reads it (see the struct's own comment history) -- the chip's real value
        // always comes from getChannelMeterPeak().
    }
    return out;
}

float TrackChannelLinkController::getChannelMeterPeak(synth::TrackId track) const {
    // The tick path. No resolve() here on purpose (see the surface's contract): the id was resolved
    // by the last getChannelInfo(), and anything that could change it -- a doc edit, a graph change
    // -- already drives refreshFromDoc()/reconcileLinkedTracks(), both of which re-resolve.
    const auto it = meterStripIds_.find(track.value);
    if (it == meterStripIds_.end())
        return 0.0f;
    auto* node = graph().getNodeForId(it->second);
    auto* strip = node != nullptr ? dynamic_cast<ChannelStripModule*>(node->getProcessor()) : nullptr;
    if (strip == nullptr)
        return 0.0f;
    // FRO146: the TrackHeader reader slot -- the ONE consumer of it (see getChannelInfo()'s own
    // comment above on why that method must never also read it).
    return std::max(strip->takeMeterPeak(synth::MeterReader::TrackHeader, 0),
                    strip->takeMeterPeak(synth::MeterReader::TrackHeader, 1));
}

// ---- (a) Names sync both ways -------------------------------------------------------------

bool TrackChannelLinkController::renameLinkedTrackAndChannel(synth::TrackId track, const juce::String& newName) {
    const auto info = resolve(track);
    if (!info.linked)
        return false;
    const auto* macro = macroForStrip(info.stripUuid);
    if (macro == nullptr)
        return false; // a hand-built, unboxed chain has no channel name to sync - see the header

    const juce::String macroId = macro->id;
    undo_.recordGraphTimelineAndMacroChange(graph(), doc_, macros(), [this, track, macroId, newName] {
        doc_.setTrackName(track, newName);
        if (auto* m = macros().find(macroId))
            m->name = newName;
    });
    graphEditor_.repaint();
    return true;
}

void TrackChannelLinkController::installMacroRenameHook() {
    // Fired from MacroGroupController::renameMacro INSTEAD of its own recordGraphAndMacroChange, so
    // the linked track's name lands in the SAME transaction: one Cmd+Z undoes both halves. Returning
    // false (no linked track) leaves renameMacro to record exactly as it always has.
    graphEditor_.getMacroController().recordMacroRenameHook =
        [this](const juce::String& macroId, const juce::String& newName, const std::function<void()>& renameMutation) {
            const auto track = linkedTrackForMacro(macroId);
            if (!track.isValid())
                return false;
            undo_.recordGraphTimelineAndMacroChange(graph(), doc_, macros(), [this, track, newName, &renameMutation] {
                renameMutation();
                doc_.setTrackName(track, newName);
            });
            return true;
        };
}

// ---- (b) Colour syncs live ----------------------------------------------------------------

std::unique_ptr<ColourPickerPopup>
TrackChannelLinkController::buildLinkedChannelColourPicker(synth::TrackId track, juce::PropertiesFile* favourites) {
    const auto info = resolve(track);
    if (!info.linked)
        return nullptr;
    const auto* macro = macroForStrip(info.stripUuid);
    if (macro == nullptr)
        return nullptr; // unboxed chain: colour sync is a no-op, the header's own picker still works
    const auto* t = doc_.getTrack(track);
    if (t == nullptr)
        return nullptr;

    const juce::String macroId = macro->id;
    const juce::uint32 originalTrackColour = t->colourArgb;
    const juce::Colour originalMacroColour = macro->colour;

    // Writes BOTH targets with no undo step at all -- the same contract the header's single-target
    // preview already has, just fanned out (docs/layout/colour-overrides.md).
    auto applyBoth = [this, track, macroId](juce::Colour c) {
        doc_.setTrackColour(track, c.getARGB());
        if (auto* m = macros().find(macroId))
            m->colour = c;
        graphEditor_.repaint(); // MacroCardComponent paints name/colour straight off the MacroSet
    };

    return std::make_unique<ColourPickerPopup>(
        juce::Colour(originalTrackColour), favourites, applyBoth,
        [this, track, macroId, originalTrackColour, originalMacroColour, applyBoth](juce::Colour finalColour) {
            if (finalColour.getARGB() == originalTrackColour) {
                // No net change: put both back exactly as they were (a preview may have nudged
                // them) and record nothing -- every other no-op close in this app does the same.
                applyBoth(juce::Colour(originalTrackColour));
                if (auto* m = macros().find(macroId))
                    m->colour = originalMacroColour;
                graphEditor_.repaint();
                return;
            }
            // ONE undo step whose undo restores BOTH originals: silently put them back first
            // (outside the recorded mutation, so that restore is not itself undoable), then perform
            // the real edit as the one recorded compound step.
            doc_.setTrackColour(track, originalTrackColour);
            if (auto* m = macros().find(macroId))
                m->colour = originalMacroColour;
            undo_.recordGraphTimelineAndMacroChange(graph(), doc_, macros(), [&] {
                doc_.setTrackColour(track, finalColour.getARGB());
                if (auto* m = macros().find(macroId))
                    m->colour = finalColour;
            });
            graphEditor_.repaint();
        });
}

// ---- (c) The track header's M/S drive the strip --------------------------------------------

bool TrackChannelLinkController::toggleLinkedChannelMuted(synth::TrackId track) {
    const auto info = resolve(track);
    auto* strip = info.linked ? stripFor(info) : nullptr;
    if (strip == nullptr || !strip->hasMuteParameter())
        return false;

    // The strip's mute is a real AudioParameterBool, so it is part of the graph's JSON and the
    // snapshot pair below is a complete undo step for it.
    undo_.captureBeforeState(graph());
    strip->setMuted(!strip->isMuted());
    undo_.pushSnapshotFromCapture(graph());
    return true;
}

bool TrackChannelLinkController::toggleLinkedChannelSoloed(synth::TrackId track) {
    const auto info = resolve(track);
    auto* strip = info.linked ? stripFor(info) : nullptr;
    if (strip == nullptr)
        return false;

    // NEVER ChannelStripModule::setSoloed() directly (Source/CLAUDE.md): the engine owns the
    // soloed-strip count the render-time gate reads. The flag lives in the strip's trusted extra
    // state, so it round-trips through the snapshot; the count is re-derived after an undo by the
    // restore hook's publishTimeline(), which recounts the whole graph.
    undo_.captureBeforeState(graph());
    engine_.setChannelStripSoloed(info.stripId, !strip->isSoloed());
    undo_.pushSnapshotFromCapture(graph());
    return true;
}

void TrackChannelLinkController::reconcileLinkedTracks() {
    if (reconciling_)
        return;
    const juce::ScopedValueSetter<bool> guard(reconciling_, true);

    // Collected first: the transfer writes the doc, and a doc write notifies listeners while this
    // loop would otherwise still be walking the track list it is mutating through.
    std::vector<synth::TrackId> muted, soloed;
    for (const auto& track : doc_.getTracks()) {
        if (!track.muted && !track.soloed)
            continue;
        const auto info = resolve(track.id);
        meterStripIds_[track.id.value] = info.stripId;
        if (!info.linked || stripFor(info) == nullptr)
            continue;
        if (track.muted)
            muted.push_back(track.id);
        if (track.soloed)
            soloed.push_back(track.id);
    }

    for (const auto id : muted) {
        const auto info = resolve(id);
        if (auto* strip = stripFor(info); strip != nullptr && strip->hasMuteParameter())
            strip->setMuted(true);
        doc_.setTrackMuted(id, false);
    }
    for (const auto id : soloed) {
        const auto info = resolve(id);
        engine_.setChannelStripSoloed(info.stripId, true);
        doc_.setTrackSoloed(id, false);
    }
}

// ---- The channel chip's click ---------------------------------------------------------------

void TrackChannelLinkController::revealChannelForTrack(synth::TrackId track) {
    const auto info = resolve(track);
    if (!info.hasChannel)
        return;

    // FRO11 (P9-5): the mixer panel's own reveal, when it exists and can show the strip's column
    // -- open/focus the dock on the Mixer tab and flash/select the column, per the ticket. Falls
    // through to the canvas reveal below when unset or unsuccessful (§5.9's "mixer hidden by
    // preference" case -- no such preference exists yet, so today this only differs before the
    // hook is installed).
    if (mixerRevealHook_ && mixerRevealHook_(info.stripId))
        return;

    // Scroll into view AND select -- the Locate Master contract (a chip's whole point is finding
    // something that may be off-screen), not the binding chip's highlight-only one.
    const auto* macro = macroForStrip(info.stripUuid);
    if (macro != nullptr) {
        const bool collapsed = macro->collapsed;
        const auto cardCentre = macro->bounds.toFloat().getCentre();
        graphEditor_.selectMacro(macro->id, /*additive=*/false);
        if (collapsed) {
            graphEditor_.centreViewOn(cardCentre);
            return;
        }
    } else {
        graphEditor_.selectModule(info.stripId, /*additive=*/false);
    }

    for (auto* comp : graphEditor_.getModuleComponents()) {
        if (comp != nullptr && comp->getNodeId() == info.stripId) {
            graphEditor_.centreViewOn(comp->getBounds().toFloat().getCentre());
            return;
        }
    }
}

} // namespace synth::ui
