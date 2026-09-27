// MainComponentTrackPresets.cpp — FRO13 (P9-7, docs/mixer/track-presets.md): "Save track as preset.../Set
// as default" (track header menu + channel macro menu), the "+ Track" grouped preset list, "Insert
// Track Preset from File...", and the per-type default consulted by addAudioTrack/
// buildInstrumentTrackAndChain (see MainComponentTrackCreation.cpp's own edits for that half).
// MainComponent is declared in MainComponent.h; the rest of its implementation lives in the
// sibling MainComponent*.cpp units next to this one.
#include "AudioEngine/AudioEngine.h"
#include "MainComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"

#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Mixer/MasterSplice.h"
#include "Mixer/MixerSends/MixerSends.h"
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

// Shared by extractAndSaveTrackPreset() and saveBusAsPreset(): extracts `macroId` (+ every outside
// module feeding it through a port) and writes it to disk under `name`. False on any failure (no
// such macro, extraction/save failure) — nothing is left half-written.
static bool extractAndSaveTrackPresetForMacro(MainComponent& mc, const juce::String& macroId,
                                              synth::TrackPresetKind kind, const juce::String& name) {
    auto preset = synth::TrackPresetManager::extractTrackPreset(mc.getAudioEngine().getGraph(),
                                                                mc.getGraphEditor().getMacros(), macroId, kind, name);
    if (!preset.isObject())
        return false;
    return synth::TrackPresetManager::saveTrackPreset(synth::TrackPresetManager::getDefaultTrackPresetsDirectory(),
                                                      name, preset);
}

// Shared by saveTrackAsPreset() and setTrackPresetAsDefault(): resolves `bindingUuid`'s channel
// macro, then delegates to extractAndSaveTrackPresetForMacro. False on any failure (no such macro,
// extraction/save failure) — nothing is left half-written.
static bool extractAndSaveTrackPreset(MainComponent& mc, const juce::String& bindingUuid, synth::TrackPresetKind kind,
                                      const juce::String& name) {
    const auto* macro = mc.getGraphEditor().getMacros().findByMember(bindingUuid);
    if (macro == nullptr)
        return false;
    return extractAndSaveTrackPresetForMacro(mc, macro->id, kind, name);
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

// FRO297 (docs/mixer/track-presets.md#a-third-kind-bus): GraphEditor::onTrackPresetMenuAction's
// handler -- resolves `macroId` to the existing per-track path when it is a bound track's own
// channel (Audio/Instrument, unchanged behaviour), or to the bus path when it is a bus's own macro
// (no bound track, hence no track header to have reached this from). A malformed/vanished macroId
// (not found, or a channel macro somehow bound to neither a track nor a bus) is a silent no-op --
// the menu that produced this click is itself gated on synth::isChannelMacro, so this should never
// see anything else in practice.
void MainComponent::handleMacroTrackPresetAction(const juce::String& macroId, bool setAsDefault) {
    for (const auto& track : timelineDoc.getTracks()) {
        if (track.bindingUuid.isEmpty())
            continue;
        const auto* macro = graphEditor.getMacros().findByMember(track.bindingUuid);
        if (macro != nullptr && macro->id == macroId) {
            if (setAsDefault)
                setTrackPresetAsDefault(track.id);
            else
                saveTrackAsPreset(track.id);
            return;
        }
    }

    // No bound track claims this macro: a bus's own macro. `setAsDefault` never reaches here true --
    // the macro's own menu omits "Set as Default Track Preset" for a bus (GraphEditorMacroPrompts.cpp,
    // synth::isBusMacro) -- but guard it anyway rather than silently mis-saving on some future caller.
    if (!setAsDefault)
        saveBusAsPreset(macroId);
}

// The bus sibling of saveTrackAsPreset() -- same async naming-prompt idiom, keyed on `macroId`
// directly (a bus has no TrackId to resolve one from). Prefills the macro's own name (what a boxed
// bus already shows, e.g. "Bus 1" or a user rename) rather than a track name.
void MainComponent::saveBusAsPreset(const juce::String& macroId) {
    const auto* macro = graphEditor.getMacros().find(macroId);
    if (macro == nullptr) {
        statusBar.showMessage("This bus is gone - nothing to save");
        return;
    }
    const juce::String startingName = macro->name.isNotEmpty() ? macro->name : juce::String("Bus");

    auto* window = new juce::AlertWindow("Save Bus Preset", "Name this bus preset:", juce::AlertWindow::NoIcon);
    window->addTextEditor("name", startingName, "Preset name:");
    window->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<MainComponent> safeThis(this);
    window->enterModalState(
        true, juce::ModalCallbackFunction::create([safeThis, window, macroId](int result) {
            std::unique_ptr<juce::AlertWindow> owned(window);
            if (result != 1)
                return;
            auto* self = safeThis.getComponent();
            if (self == nullptr)
                return;

            auto name = synth::TrackPresetManager::sanitiseName(owned->getTextEditorContents("name"));
            if (name.isEmpty()) {
                self->statusBar.showMessage("Bus preset not saved - the name was empty or reserved");
                return;
            }
            if (extractAndSaveTrackPresetForMacro(*self, macroId, synth::TrackPresetKind::Bus, name))
                self->statusBar.showMessage("Saved bus preset \"" + name + "\"");
            else
                self->statusBar.showMessage("Could not save bus preset \"" + name + "\"");
        }),
        false);
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

// FRO297 (docs/mixer/track-presets.md#a-third-kind-bus): the Bus-kind sibling of
// insertTrackFromPresetVar. A bus preset creates NO timeline track -- just the bus chain, which the
// mixer shows as a BUS column -- so there is no track source node to find and no TimelineDoc::addTrack.
// It still needs the same Strip->Master wiring (a preset never captures the shared Master singleton)
// and the same "isBus" re-flag every other "Add bus" builder applies -- extractTrackPreset scrubs
// "isBus" from the saved JSON (TrackPresetManager.cpp), so it must be set again here, the same
// mechanism synth::buildBusChannel uses for a freshly built bus. NO UNDO TRANSACTION OF ITS OWN.
// Returns the inserted bus's macro name, or empty on rejection/failure.
juce::String MainComponent::insertBusFromPresetVar(const juce::var& preset) {
    if (!preset.isObject())
        return {};

    auto& graph = audioEngine.getGraph();

    // Same left-to-right placement idiom insertTrackFromPresetVar uses.
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

    juce::AudioProcessorGraph::Node* stripNode = nullptr;
    for (const auto id : added) {
        auto* node = graph.getNodeForId(id);
        if (node != nullptr && dynamic_cast<ChannelStripModule*>(node->getProcessor()) != nullptr)
            stripNode = node;
    }
    if (stripNode == nullptr)
        return {}; // malformed preset: no Channel Strip -- nothing here is a channel at all

    // The scrubbed re-flag -- see this function's own comment above.
    if (auto* strip = dynamic_cast<ChannelStripModule*>(stripNode->getProcessor()))
        strip->setIsBus(true);

    if (auto* master = synth::spliceMasterNode(graph, dropPos)) {
        graph.addConnection({{stripNode->nodeID, 0}, {master->nodeID, MasterModule::kMixLeft}});
        graph.addConnection(
            {{stripNode->nodeID, ChannelStripModule::kRightBase}, {master->nodeID, MasterModule::kMixRight}});
    }

    // outMacros straight onto the live MacroSet, same reason insertTrackFromPresetVar's own comment
    // gives -- addMacroForMembers would re-derive membership/bounds from scratch and discard the
    // preset's own captured ports/bounds.
    for (auto& macro : outMacros)
        graphEditor.getMacros().add(macro);

    // Name/box it the way "Add bus" does ("Bus N") unless the preset's own captured macro already
    // carries a name (docs/mixer/track-presets.md#a-third-kind-bus) -- a source bus that was
    // user-renamed before saving arrives with that name already on its captured macro; only a
    // preset whose macro came back nameless falls back to the numbered default.
    const juce::String stripUuid = stripNode->properties["uuid"].toString();
    auto* insertedMacro = graphEditor.getMacros().findByMember(stripUuid);
    juce::String busName = insertedMacro != nullptr ? insertedMacro->name : juce::String();
    if (busName.isEmpty()) {
        busName = synth::busFallbackName(graph, stripNode->nodeID);
        if (insertedMacro != nullptr)
            insertedMacro->name = busName;
    }

    graphEditor.updateComponents();
    return busName;
}

void MainComponent::addBusFromPreset(const juce::String& presetName) {
    auto preset = synth::TrackPresetManager::loadTrackPreset(
        synth::TrackPresetManager::getDefaultTrackPresetsDirectory(), presetName);
    if (!preset.isObject()) {
        statusBar.showMessage("Could not load bus preset \"" + presetName + "\"");
        return;
    }

    // No timeline change at all -- a bus has no track -- so this is recordGraphAndMacroChange, the
    // same two-domain transaction MixerPanelComponent::createBus() uses for a freshly built bus,
    // not recordGraphTimelineAndMacroChange.
    juce::String busName;
    const bool pushed =
        undoManager.recordGraphAndMacroChange(audioEngine.getGraph(), graphEditor.getMacros(),
                                              [this, &preset, &busName] { busName = insertBusFromPresetVar(preset); });

    // No track binding to reconcile, but the mixer's own column set (and, for a hosted build, the
    // published render-thread snapshot) must still catch up to the graph change -- same reason
    // addTrackFromPreset()/insertTrackPresetFromFile() call this after their own insert.
    reconcileTimelineAfterGraphChange();
    statusBar.showMessage(pushed && busName.isNotEmpty() ? "Added " + busName
                                                         : "Could not insert bus preset \"" + presetName + "\"");
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

    // FRO297: a Bus-kind file has no track to create -- same recordGraphAndMacroChange/
    // insertBusFromPresetVar path addBusFromPreset() uses, not the track-creating transaction below.
    if (kind == synth::TrackPresetKind::Bus) {
        juce::String busName;
        const bool pushed = undoManager.recordGraphAndMacroChange(
            audioEngine.getGraph(), graphEditor.getMacros(),
            [this, &preset, &busName] { busName = insertBusFromPresetVar(preset); });
        reconcileTimelineAfterGraphChange();
        statusBar.showMessage(pushed && busName.isNotEmpty() ? "Added " + busName
                                                             : "Could not insert \"" + file.getFileName() + "\"");
        return busName;
    }

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
