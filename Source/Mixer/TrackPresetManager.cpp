#include "TrackPresetManager.h"

#include "Branding.h"
#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Modules/ChannelStripModule.h"
#include "SnippetManager.h"
#include <algorithm>
#include <map>

namespace synth {

namespace {

// AIStateMapper::graphToJSON's factory key for a Channel Strip node — see
// AIStateMapper::getFactoryTypeName / AIStateMapperInternal.h's factory table.
constexpr const char* kChannelStripTypeName = "Channel Strip";

struct TrackPresetNameComparator {
    static int compareElements(const TrackPresetInfo& a, const TrackPresetInfo& b) {
        return a.name.compareIgnoreCase(b.name);
    }
};

} // namespace

// ---------------------------------------------------------------------------------------
// Storage location
// ---------------------------------------------------------------------------------------

juce::File TrackPresetManager::getDefaultTrackPresetsDirectory() {
    juce::File folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                            .getChildFile(synth::branding::kSettingsFolderName)
                            .getChildFile("TrackPresets");

    if (!folder.exists())
        folder.createDirectory();

    return folder;
}

// ---------------------------------------------------------------------------------------
// Extraction
// ---------------------------------------------------------------------------------------

juce::var TrackPresetManager::extractTrackPreset(juce::AudioProcessorGraph& graph, const MacroSet& macros,
                                                 const juce::String& channelMacroId, TrackPresetKind kind,
                                                 const juce::String& name) {
    const Macro* macro = macros.find(channelMacroId);
    if (macro == nullptr)
        return {};

    // uuid -> live NodeID, so the macro's uuid-keyed membership (Step 1, "the own box") and the
    // outside-modulator walk's NodeID-keyed result (Step 2) can be combined into one selection.
    std::map<juce::String, NodeID> nodeForUuid;
    for (auto* node : graph.getNodes()) {
        const juce::String uuid = node->properties["uuid"].toString();
        if (uuid.isNotEmpty())
            nodeForUuid[uuid] = node->nodeID;
    }

    std::vector<NodeID> selection;
    NodeID stripNodeId; // this macro's own ChannelStripModule member, if any (defensive default -
                        // invalid NodeID{} when the macro somehow carries none).
    for (const auto& uuid : macro->members) {
        auto it = nodeForUuid.find(uuid);
        if (it == nodeForUuid.end())
            continue; // a stale member (its node is gone) — same "skip it" posture the rest of the
                      // codebase takes on a dead macro member.
        selection.push_back(it->second);
        if (auto* node = graph.getNodeForId(it->second))
            if (dynamic_cast<ChannelStripModule*>(node->getProcessor()) != nullptr)
                stripNodeId = it->second;
    }
    if (selection.empty())
        return {};

    // Step 2 (founder requirement, docs/mixer/track-presets.md#what-a-saved-preset-carries-beyond-the-box): every
    // outside module feeding this channel through a port (or a raw un-ported jack), transitively.
    for (const auto id : collectOutsideModulatorsForTrackPreset(graph, macros, channelMacroId))
        if (std::find(selection.begin(), selection.end(), id) == selection.end())
            selection.push_back(id);

    // Step 3: extract verbatim through SnippetManager::extractSnippet — its existing "capture a
    // macro only when EVERY member is inside selection" rule is exactly what makes the channel
    // macro travel intact while an outside module (never fully enclosed, by construction) stays a
    // bare, unmacro'd node, per the founder's "arrive as fresh copies beside the track's box"
    // requirement. includeExtraState=true is forced: a track preset must always carry strip
    // shape/gain/pan, or a Mono strip would silently reload as the Stereo default.
    auto preset = SnippetManager::extractSnippet(graph, selection, name, /*includeExtraState=*/true, macros);
    auto* root = preset.getDynamicObject();
    if (root == nullptr)
        return {};

    root->setProperty("schemaVersion", 1);
    root->setProperty("trackPresetKind", kind == TrackPresetKind::Instrument ? "instrument" : "audio");

    // channelMacroId: the snippet-local id of this macro's own Channel Strip member (see the
    // class comment on TrackPresetManager for why the load path doesn't need to consume this).
    if (!(stripNodeId == NodeID{}))
        root->setProperty("channelMacroId", (int)stripNodeId.uid);

    // Scrub "solo", "isBus", "sends", and "name" from every captured Channel Strip's extra state
    // (root CLAUDE.md tripwire): an imported soloed_=true would silence the whole mix render-wide,
    // regardless of includeExtraState being forced on above for the shape/gain/pan it legitimately
    // carries. "isBus" and "sends" (FRO15) get the same treatment: a preset captured from a bus
    // strip must not badge an ordinary track channel as BUS wherever it's inserted, and a preset
    // captured from a strip with configured sends must not restore slots whose cable target was
    // never captured (a saved send target is a graph edge, never stored, per
    // docs/mixer/sends-and-buses.md) -- carrying the slot state alone would show "No target" rows on every insert.
    // "name" (FRO225, docs/mixer/panel.md) is the SOURCE strip's own user-given identity -- applying a
    // preset captured from "Lead Vox" to a brand new "Backing Vox" channel must not rename it out
    // from under the user; the destination strip keeps whatever name (or none) it already had.
    if (auto* nodesArr = root->getProperty("nodes").getArray()) {
        for (auto& nVar : *nodesArr) {
            auto* nObj = nVar.getDynamicObject();
            if (nObj == nullptr || nObj->getProperty("type").toString() != kChannelStripTypeName)
                continue;
            if (auto* stateObj = nObj->getProperty("state").getDynamicObject()) {
                stateObj->removeProperty("solo");
                stateObj->removeProperty("isBus");
                stateObj->removeProperty("sends");
                stateObj->removeProperty("name");
            }
        }
    }

    return preset;
}

TrackPresetKind TrackPresetManager::getPresetKind(const juce::var& preset) {
    if (auto* obj = preset.getDynamicObject())
        if (obj->getProperty("trackPresetKind").toString() == "instrument")
            return TrackPresetKind::Instrument;
    return TrackPresetKind::Audio;
}

// ---------------------------------------------------------------------------------------
// Insertion
// ---------------------------------------------------------------------------------------

std::vector<TrackPresetManager::NodeID> TrackPresetManager::insertTrackPreset(const juce::var& preset,
                                                                              juce::AudioProcessorGraph& graph,
                                                                              juce::Point<int> dropPos,
                                                                              std::vector<Macro>* outMacros) {
    // includeExtraState=true: see extractTrackPreset's own comment — a deliberate, permanent
    // difference from every other insertSnippet caller besides the in-memory clipboard.
    // trustedPayload=false: a track preset is a file on disk like a snippet, so it goes through
    // the SAME untrusted validatePatch gate before the trusted, exact-subgraph apply — the
    // SnippetManager::insertSnippet / ProjectBundle::load pairing docs/mixer/track-presets.md and the root
    // CLAUDE.md both name. validatePatch's untrusted path does not itself inspect a node's
    // "state" key (only applyExtraStateToProcessor does, gated on trusted=true, which
    // insertSnippet's apply call always is) — so this really is the thin wrapper it looks like,
    // not one that silently skips the untrusted gate.
    return SnippetManager::insertSnippet(preset, graph, dropPos, /*includeExtraState=*/true, outMacros,
                                         /*trustedPayload=*/false);
}

// ---------------------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------------------

juce::String TrackPresetManager::sanitiseName(const juce::String& raw) {
    auto safe = SnippetManager::sanitiseName(raw);
    if (safe.equalsIgnoreCase(kFactoryDefaultSentinel))
        return {}; // reserved for the Preferences dropdown's "use the factory chain" sentinel
    return safe;
}

juce::File TrackPresetManager::fileForName(const juce::File& dir, const juce::String& name) {
    auto safe = sanitiseName(name);
    if (safe.isEmpty())
        return {};
    return dir.getChildFile(safe + kFileExtension);
}

bool TrackPresetManager::saveTrackPreset(const juce::File& dir, const juce::String& name, const juce::var& preset) {
    auto safe = sanitiseName(name);
    if (safe.isEmpty())
        return false;
    if (SnippetManager::getModuleCount(preset) <= 0)
        return false;

    if (!dir.exists() && !dir.createDirectory())
        return false;

    // Persist the sanitised name so the menu label and the filename can never disagree — same
    // idiom as SnippetManager::saveSnippet.
    juce::var stored = preset;
    if (auto* obj = stored.getDynamicObject())
        obj->setProperty("name", safe);

    auto file = dir.getChildFile(safe + kFileExtension);
    return file.replaceWithText(juce::JSON::toString(stored));
}

juce::var TrackPresetManager::loadTrackPreset(const juce::File& dir, const juce::String& name) {
    auto file = fileForName(dir, name);
    if (file == juce::File() || !file.existsAsFile())
        return {};
    auto json = juce::JSON::parse(file.loadFileAsString());
    if (!json.isObject())
        return {};
    return json;
}

juce::var TrackPresetManager::loadTrackPresetFile(const juce::File& file) {
    if (!file.existsAsFile())
        return {};
    auto json = juce::JSON::parse(file.loadFileAsString());
    if (!json.isObject())
        return {};
    return json;
}

juce::Array<TrackPresetInfo> TrackPresetManager::listTrackPresets(const juce::File& dir, TrackPresetKind kind) {
    juce::Array<TrackPresetInfo> result;
    if (!dir.isDirectory())
        return result;

    juce::Array<juce::File> files;
    dir.findChildFiles(files, juce::File::findFiles, false, juce::String("*") + kFileExtension);

    for (const auto& file : files) {
        auto json = juce::JSON::parse(file.loadFileAsString());
        if (!json.isObject())
            continue; // unreadable/corrupt file: skip it rather than surface a broken row
        if (getPresetKind(json) != kind)
            continue;

        TrackPresetInfo info;
        info.file = file;
        info.name = SnippetManager::getSnippetName(json);
        if (info.name.isEmpty())
            info.name = file.getFileNameWithoutExtension();
        info.kind = kind;
        info.moduleCount = SnippetManager::getModuleCount(json);
        result.add(info);
    }

    TrackPresetNameComparator comparator;
    result.sort(comparator);

    return result;
}

bool TrackPresetManager::deleteTrackPreset(const juce::File& dir, const juce::String& name) {
    auto file = fileForName(dir, name);
    if (file == juce::File() || !file.existsAsFile())
        return false;
    return file.deleteFile();
}

} // namespace synth
