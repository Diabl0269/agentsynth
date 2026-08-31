#include "ProjectBundle.h"
#include "Branding.h"
#include "Timeline/TimelineReconciler.h"

namespace synth {

namespace {
constexpr const char* kTimelineKey = "timeline";
} // namespace

bool ProjectBundle::isBundle(const juce::File& dir) {
    if (!dir.isDirectory())
        return false;
    if (!dir.getFileName().endsWithIgnoreCase(kBundleExtension))
        return false;
    return dir.getChildFile(kProjectFileName).existsAsFile();
}

ProjectLoadResult ProjectBundle::save(const juce::File& bundleDir, juce::AudioProcessorGraph& graph,
                                      const TimelineDoc& timeline, PatchDocument& patchDocument) {
    if (!bundleDir.exists() && !bundleDir.createDirectory())
        return {false, "io: could not create bundle directory \"" + bundleDir.getFullPathName() + "\"."};

    auto audioDir = bundleDir.getChildFile(kAudioSubdirName);
    if (!audioDir.exists() && !audioDir.createDirectory())
        return {false, "io: could not create \"" + audioDir.getFullPathName() + "\"."};

    auto peaksDir = bundleDir.getChildFile(kPeaksSubdirName);
    if (!peaksDir.exists() && !peaksDir.createDirectory())
        return {false, "io: could not create \"" + peaksDir.getFullPathName() + "\"."};

    auto json = AIStateMapper::graphToJSON(graph);
    // Re-merge whatever unknown top-level keys were stashed on this bundle's last load — mirrors
    // GraphEditor::savePreset. A stale "timeline" among them (e.g. this document started life as a
    // plain .json that already carried one) is overwritten below: the live TimelineDoc is
    // authoritative on save, not whatever happened to be stashed.
    json = patchDocument.toVar(json);

    auto* rootObj = json.getDynamicObject();
    if (rootObj == nullptr)
        return {false, "io: graphToJSON did not produce a JSON object."};
    // Set LAST so a fresh timeline always wins over a stashed one.
    rootObj->setProperty(kTimelineKey, timeline.toVar());

    auto projectFile = bundleDir.getChildFile(kProjectFileName);
    // juce::File::replaceWithText already writes to a temp file and renames over the target.
    if (!projectFile.replaceWithText(juce::JSON::toString(json)))
        return {false, "io: could not write \"" + projectFile.getFullPathName() + "\"."};

    return {true, {}};
}

ProjectLoadResult ProjectBundle::load(const juce::File& bundleDir, juce::AudioProcessorGraph& graph,
                                      TimelineDoc& timeline, PatchDocument& patchDocument) {
    auto projectFile = bundleDir.getChildFile(kProjectFileName);
    if (!projectFile.existsAsFile())
        return {false, "io: \"" + projectFile.getFullPathName() + "\" does not exist."};

    auto json = juce::JSON::parse(projectFile);
    auto* rootObj = json.getDynamicObject();
    if (!json.isObject() || rootObj == nullptr)
        return {false, "parse: \"" + projectFile.getFullPathName() + "\" is not a JSON object."};

    // Detach "timeline" from the root BEFORE the untrusted gate below. validatePatch(trusted=false)
    // refuses a patch carrying a "timeline" key outright (PatchValidationError::TimelineNotAllowed)
    // — that check exists to stop provider-authored output from smuggling timeline/automation data
    // in. A .agsproj's "timeline" is this format's own dialect, not provider output, so it is pulled
    // out first and validated on its own terms below rather than tripping a check meant for a
    // different threat.
    const bool hasTimelineKey = rootObj->hasProperty(kTimelineKey);
    juce::var detachedTimelineVar;
    if (hasTimelineKey) {
        detachedTimelineVar = rootObj->getProperty(kTimelineKey);
        rootObj->removeProperty(kTimelineKey);
    }
    // From here on `json`/`rootObj` is "the patch" — timeline-stripped.

    // Step 1: the untrusted gate. project.json is a file on disk, hand-editable exactly like a
    // preset or a snippet — a malformed patch is rejected whole, never partially applied.
    // allowInternalModuleTypes: a bundle we wrote contains internal nodes by construction (an
    // Attenuverter per mod routing, a Track In per timeline track), and this gate is about
    // structural tampering, not about who was allowed to author the patch.
    auto validation = AIStateMapper::validatePatch(json, graph, /*clearExisting=*/true, /*trusted=*/false,
                                                   /*allowInternalModuleTypes=*/true);
    if (!validation.ok)
        return {false, "patch validation failed: " + validation.message};

    // Step 2: validate the timeline into a LOCAL doc — nothing has been touched yet. A missing key
    // is not an error (a plain patch being opened/upgraded into a bundle starts with an empty
    // timeline); a present-but-malformed one is rejected whole, same as the patch above.
    TimelineDoc localTimeline;
    if (hasTimelineKey && !localTimeline.fromVar(detachedTimelineVar))
        return {false,
                "timeline validation failed: malformed \"timeline\" in \"" + projectFile.getFullPathName() + "\"."};

    // Both validations passed — only now does anything mutate.
    if (!AIStateMapper::applyJSONToGraph(json, graph, /*clearExisting=*/true, /*trusted=*/true))
        return {false, "io: applyJSONToGraph rejected a patch that had already passed validation."};

    // The timeline-stripped root, so "timeline" is never double-stored in the stash.
    patchDocument.loadFromVar(json);

    // Move the pre-validated timeline state into the live doc. fromVar is all-or-nothing and this
    // exact var was already proven valid against `localTimeline` above, so this cannot fail.
    if (hasTimelineKey)
        timeline.fromVar(detachedTimelineVar);
    else
        timeline.clear();

    // A track's bindingUuid or a lane's nodeUuid that no longer resolves to any live node's
    // "uuid" is retained and flagged `orphaned`, never deleted.
    TimelineReconciler::reconcile(timeline, graph);

    return {true, {}};
}

juce::File ProjectBundle::getDefaultProjectsDirectory() {
    juce::File folder =
        juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile(branding::kProjectsFolderName);

    if (!folder.exists())
        folder.createDirectory();

    return folder;
}

} // namespace synth
