// MainComponentTrackPresets.cpp — FRO13 (P9-7, docs/mixer/track-presets.md): "Save track as preset.../Set
// as default" (track header menu + channel macro menu), the "+ Track" grouped preset list, "Insert
// Track Preset from File...", and the per-type default consulted by addAudioTrack/
// buildInstrumentTrackAndChain (see MainComponentTrackCreation.cpp's own edits for that half).
// MainComponent is declared in MainComponent.h; the rest of its implementation lives in the
// sibling MainComponent*.cpp units next to this one.
#include "MainComponent.h"

#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Mixer/MasterSplice.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/MasterModule.h"
#include "UI/Timeline/TrackColour.h"

namespace {

// Horizontal gap between the new track's cards, same value MainComponentTrackCreation.cpp's own
// kChannelCardGapX uses (duplicated rather than shared — it is a layout-cosmetic literal local to
// each unit that lays out cards, not a cross-file contract).
constexpr int kTrackPresetCardGapX = 40;

// FRO13 (P9-7, docs/mixer/track-presets.md#saving-and-setting-a-default): the per-type default track preset settings
// keys — duplicated from PreferencesSettingsTabInternal.h's own copy for the same "one-line string not worth a header
// dependency" reason every other cross-file settings key in this codebase is (see MainComponentFileIO.cpp's own
// kAutosaveEnabledKey). The two writers/readers MUST agree on the string values.
constexpr const char* kMixerDefaultTrackPresetAudioKey = "mixerDefaultTrackPresetAudio";
constexpr const char* kMixerDefaultTrackPresetInstrumentKey = "mixerDefaultTrackPresetInstrument";

const char* mixerDefaultTrackPresetKey(synth::TrackPresetKind kind) {
    return kind == synth::TrackPresetKind::Instrument ? kMixerDefaultTrackPresetInstrumentKey
                                                      : kMixerDefaultTrackPresetAudioKey;
}

} // namespace

// FRO13 (P9-7): true when `track`'s bound node has a channel of its own — gates the header menu's
// and the channel macro menu's "Save track as preset.../Set as default" pair.
bool MainComponent::canSaveTrackPresetForTrack(synth::TrackId trackId) const {
    const auto* track = timelineDoc.getTrack(trackId);
    if (track == nullptr || track->bindingUuid.isEmpty())
        return false;
    return graphEditor.isChannelMacroForTrack(track->bindingUuid);
}

// Shared by saveTrackAsPreset() and setTrackPresetAsDefault(): resolves `bindingUuid`'s channel
// macro, extracts it (+ every outside module feeding it through a port) and writes it to disk
// under `name`. False on any failure (no such macro, extraction/save failure) — nothing is left
// half-written.
static bool extractAndSaveTrackPreset(MainComponent& mc, const juce::String& bindingUuid, synth::TrackPresetKind kind,
                                      const juce::String& name) {
    auto& macros = mc.getGraphEditor().getMacros();
    const auto* macro = macros.findByMember(bindingUuid);
    if (macro == nullptr)
        return false;
    auto preset =
        synth::TrackPresetManager::extractTrackPreset(mc.getAudioEngine().getGraph(), macros, macro->id, kind, name);
    if (!preset.isObject())
        return false;
    return synth::TrackPresetManager::saveTrackPreset(synth::TrackPresetManager::getDefaultTrackPresetsDirectory(),
                                                      name, preset);
}

void MainComponent::saveTrackAsPreset(synth::TrackId trackId) {
    if (!canSaveTrackPresetForTrack(trackId)) {
        statusBar.showMessage("This track has no channel of its own yet - use Make Channel first");
        return;
    }
    const auto* track = timelineDoc.getTrack(trackId);
    const juce::String bindingUuid = track->bindingUuid;
    const auto kind =
        track->kind == synth::TrackKind::Audio ? synth::TrackPresetKind::Audio : synth::TrackPresetKind::Instrument;

    // Same async AlertWindow naming-prompt idiom as promptSaveSnippet() (MainComponentPanels.cpp).
    auto* window = new juce::AlertWindow("Save Track Preset", "Name this track preset:", juce::AlertWindow::NoIcon);
    window->addTextEditor("name", track->name, "Preset name:");
    window->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<MainComponent> safeThis(this);
    window->enterModalState(
        true, juce::ModalCallbackFunction::create([safeThis, window, bindingUuid, kind](int result) {
            std::unique_ptr<juce::AlertWindow> owned(window);
            if (result != 1)
                return;
            auto* self = safeThis.getComponent();
            if (self == nullptr)
                return;

            auto name = synth::TrackPresetManager::sanitiseName(owned->getTextEditorContents("name"));
            if (name.isEmpty()) {
                self->statusBar.showMessage("Track preset not saved - the name was empty or reserved");
                return;
            }
            if (extractAndSaveTrackPreset(*self, bindingUuid, kind, name))
                self->statusBar.showMessage("Saved track preset \"" + name + "\"");
            else
                self->statusBar.showMessage("Could not save track preset \"" + name + "\"");
        }),
        false);
}

void MainComponent::setTrackPresetAsDefault(synth::TrackId trackId) {
    if (!canSaveTrackPresetForTrack(trackId)) {
        statusBar.showMessage("This track has no channel of its own yet - use Make Channel first");
        return;
    }
    const auto* track = timelineDoc.getTrack(trackId);
    const auto kind =
        track->kind == synth::TrackKind::Audio ? synth::TrackPresetKind::Audio : synth::TrackPresetKind::Instrument;

    // One-or-two-click requirement (docs/mixer/track-presets.md#saving-and-setting-a-default): auto-save under a
    // generated name right now rather than requiring "Save track as preset..." to have already been run.
    const auto name = synth::TrackPresetManager::sanitiseName(track->name + " (default)");
    if (name.isEmpty() || !extractAndSaveTrackPreset(*this, track->bindingUuid, kind, name)) {
        statusBar.showMessage("Could not set the default track preset");
        return;
    }

    appProperties.getUserSettings()->setValue(mixerDefaultTrackPresetKey(kind), name);
    appProperties.getUserSettings()->saveIfNeeded();
    statusBar.showMessage(
        "Set \"" + name + "\" as the default " +
        (kind == synth::TrackPresetKind::Instrument ? juce::String("Instrument") : juce::String("Audio")) +
        " track preset");
}

// The shared insert path (default-consulting branch, "+ Track" preset list, "Insert from File...").
// NO UNDO TRANSACTION OF ITS OWN — the default-consulting branch is already inside
// addAudioTrack's own; callers that aren't already inside one wrap this in
// recordGraphTimelineAndMacroChange themselves. Returns the created track's name, or an empty
// string on rejection/failure.
juce::String MainComponent::insertTrackFromPresetVar(const juce::var& preset, synth::TrackPresetKind kind,
                                                     const juce::String& trackNamePrefix) {
    if (!preset.isObject())
        return {};

    auto& graph = audioEngine.getGraph();

    // Placement: to the right of the rightmost existing card, so a preset never lands on top of an
    // existing track's box — the same left-to-right arrangement addAudioTrack's own chain uses.
    int rightEdge = 0;
    for (auto* node : graph.getNodes()) {
        if (node == nullptr)
            continue;
        const int x = (int)node->properties.getWithDefault("x", 0);
        const juce::String typeName =
            node->getProcessor() != nullptr ? node->getProcessor()->getName() : juce::String();
        rightEdge = juce::jmax(rightEdge, x + GraphEditor::estimateModuleSize(typeName).x);
    }
    const juce::Point<int> dropPos{rightEdge + kTrackPresetCardGapX, 0};

    std::vector<synth::Macro> outMacros;
    const auto added = synth::TrackPresetManager::insertTrackPreset(preset, graph, dropPos, &outMacros);
    if (added.empty())
        return {}; // rejected by the untrusted gate, or nothing usable in the file

    juce::AudioProcessorGraph::Node* sourceNode = nullptr;
    juce::AudioProcessorGraph::Node* stripNode = nullptr;
    for (const auto id : added) {
        auto* node = graph.getNodeForId(id);
        if (node == nullptr)
            continue;
        if (synth::isTrackSourceNode(node->getProcessor()))
            sourceNode = node;
        if (dynamic_cast<ChannelStripModule*>(node->getProcessor()) != nullptr)
            stripNode = node;
    }
    if (sourceNode == nullptr)
        return {}; // malformed preset: no track source node — nothing to bind a track to

    // The strip never travels wired to Master — a preset's capture never includes the shared
    // Master singleton, the same reason a snippet never captures Audio Output (SnippetManager's own
    // "self-contained" rule). Wire it now, exactly like buildChannelChain's own Strip->Master edges.
    if (stripNode != nullptr) {
        auto* master = synth::spliceMasterNode(graph, dropPos);
        if (master != nullptr) {
            graph.addConnection({{stripNode->nodeID, 0}, {master->nodeID, MasterModule::kMixLeft}});
            graph.addConnection(
                {{stripNode->nodeID, ChannelStripModule::kRightBase}, {master->nodeID, MasterModule::kMixRight}});
        }
    }

    const int index = (int)timelineDoc.getTracks().size();
    const juce::String trackName = trackNamePrefix + " " + juce::String(index + 1);
    const auto trackKind =
        kind == synth::TrackPresetKind::Instrument ? synth::TrackKind::Midi : synth::TrackKind::Audio;
    const auto trackId = timelineDoc.addTrack(trackKind, trackName);
    if (!trackId.isValid())
        return {}; // at kMaxTracks: the inserted nodes stay as a harmless orphan (recordCombinedChange-
                   // style callers RECORD the mutation, they don't undo it — same posture addAudioTrack
                   // documents for its own factory-chain build)

    timelineDoc.setTrackBinding(trackId, sourceNode->properties["uuid"].toString());
    timelineDoc.setTrackColour(trackId, synth::ui::trackPaletteColour(index).getARGB());

    // outMacros straight onto the live MacroSet, NOT addMacroForMembers — that path re-derives
    // membership/bounds from scratch and would discard the preset's own captured ports/bounds
    // (SnippetManager::insertSnippet's existing outMacros contract).
    for (auto& macro : outMacros)
        graphEditor.getMacros().add(macro);

    graphEditor.updateComponents();
    return trackName;
}

void MainComponent::addTrackFromPreset(const juce::String& presetName, synth::TrackPresetKind kind) {
    auto preset = synth::TrackPresetManager::loadTrackPreset(
        synth::TrackPresetManager::getDefaultTrackPresetsDirectory(), presetName);
    if (!preset.isObject()) {
        statusBar.showMessage("Could not load track preset \"" + presetName + "\"");
        return;
    }

    const juce::String prefix =
        kind == synth::TrackPresetKind::Instrument ? juce::String("Instrument") : juce::String("Audio");
    juce::String trackName;
    const bool pushed = undoManager.recordGraphTimelineAndMacroChange(
        audioEngine.getGraph(), timelineDoc, graphEditor.getMacros(),
        [this, &preset, kind, prefix, &trackName] { trackName = insertTrackFromPresetVar(preset, kind, prefix); });

    reconcileTimelineAfterGraphChange();
    statusBar.showMessage(pushed && trackName.isNotEmpty() ? "Added " + trackName
                                                           : "Could not insert track preset \"" + presetName + "\"");
}

// Shared by addTrackFromPresetFile()'s FileChooser callback and the headless test seam
// (insertTrackPresetFromFileForTest, MainComponent.h) -- there is no real display in a test
// process for a juce::FileChooser to be positioned on (same reason openAddTrackMenu() below is a
// protected virtual), so tests inject the file directly here instead of driving a chooser.
// Returns the inserted track's name, or an empty string on rejection/failure (unreadable file, or
// insertTrackFromPresetVar's own rejection) -- nothing is added to the timeline or the graph in
// that case.
juce::String MainComponent::insertTrackPresetFromFile(const juce::File& file) {
    auto preset = synth::TrackPresetManager::loadTrackPresetFile(file);
    if (!preset.isObject()) {
        statusBar.showMessage("Could not load \"" + file.getFileName() + "\"");
        return {};
    }
    const auto kind = synth::TrackPresetManager::getPresetKind(preset);
    const juce::String prefix =
        kind == synth::TrackPresetKind::Instrument ? juce::String("Instrument") : juce::String("Audio");

    juce::String trackName;
    const bool pushed = undoManager.recordGraphTimelineAndMacroChange(
        audioEngine.getGraph(), timelineDoc, graphEditor.getMacros(),
        [this, &preset, kind, prefix, &trackName] { trackName = insertTrackFromPresetVar(preset, kind, prefix); });

    reconcileTimelineAfterGraphChange();
    statusBar.showMessage(pushed && trackName.isNotEmpty() ? "Added " + trackName
                                                           : "Could not insert \"" + file.getFileName() + "\"");
    return trackName;
}

void MainComponent::addTrackFromPresetFile() {
    fileChooser = std::make_unique<juce::FileChooser>("Insert Track Preset from File...",
                                                      synth::TrackPresetManager::getDefaultTrackPresetsDirectory(),
                                                      juce::String("*") + synth::TrackPresetManager::kFileExtension);
    juce::Component::SafePointer<MainComponent> safeThis(this);
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [safeThis](const juce::FileChooser& fc) {
                                 auto* self = safeThis.getComponent();
                                 if (self == nullptr)
                                     return;
                                 auto file = fc.getResult();
                                 if (file == juce::File())
                                     return;
                                 self->insertTrackPresetFromFile(file);
                             });
}
