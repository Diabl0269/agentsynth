#include "PlanPreset.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AI/PatchEval.h"
#include "SnippetManager.h"
#include "UI/Layout/LayoutUtil.h"

#include <algorithm>
#include <map>

namespace synth::planpreset {

namespace {

// Card footprint used to space the columns and rows of a plan that gave no positions: the widest card and a row
// pitch above the tallest one (the Sampler, 657), so nothing overlaps. Auto-arrange tidies it further.
constexpr int kColumnPitch = 340;
constexpr int kRowPitch = 700;

// The patch part of the plan and nothing else: the timeline ops, and the merge-only "mode" / "remove" keys, belong
// to the plan, not to a preset.
juce::var patchPartOf(const juce::var& plan) {
    auto* root = plan.getDynamicObject();
    if (root == nullptr)
        return {};
    juce::DynamicObject::Ptr patch = new juce::DynamicObject();
    for (const char* key : {"nodes", "connections", "modulations"})
        if (root->hasProperty(key))
            patch->setProperty(key, root->getProperty(key));
    return juce::var(patch.get());
}

int nodeCountOf(const juce::var& patch) {
    if (auto* nodes = patch.getProperty("nodes", {}).getArray())
        return nodes->size();
    return 0;
}

juce::String firstProblem(const synth::PatchValidationResult& validation) {
    return validation.message.isNotEmpty() ? validation.message : juce::String("The patch failed validation.");
}

// A new node's column is its longest signal path from a source; rows follow node order inside a column. Cycles
// (a feedback cable) stop the relaxation after one pass per node.
void layOutUnpositioned(juce::AudioProcessorGraph& graph) {
    std::map<juce::uint32, int> depth;
    for (auto* node : graph.getNodes())
        depth[node->nodeID.uid] = 0;
    const auto connections = graph.getConnections();
    for (size_t pass = 0; pass < depth.size(); ++pass) {
        bool changed = false;
        for (const auto& c : connections) {
            const int next = depth[c.source.nodeID.uid] + 1;
            if (next > depth[c.destination.nodeID.uid] && next < (int)depth.size()) {
                depth[c.destination.nodeID.uid] = next;
                changed = true;
            }
        }
        if (!changed)
            break;
    }
    std::map<int, int> rowInColumn;
    for (auto* node : graph.getNodes()) {
        if (node->properties.contains("x") && node->properties.contains("y"))
            continue;
        const int column = depth[node->nodeID.uid];
        const int row = rowInColumn[column]++;
        node->properties.set("x", synth::LayoutUtil::kArrangeOriginX + column * kColumnPitch);
        node->properties.set("y", synth::LayoutUtil::kArrangeOriginY + row * kRowPitch);
    }
}

} // namespace

bool patchValidates(const juce::String& planJson) {
    static std::map<juce::String, bool> answers; // message thread only, like every caller
    if (const auto it = answers.find(planJson); it != answers.end())
        return it->second;

    const auto patch = patchPartOf(juce::JSON::parse(planJson));
    juce::AudioProcessorGraph empty;
    const bool ok = nodeCountOf(patch) > 0 && AIStateMapper::validatePatch(patch, empty, /*clearExisting=*/true,
                                                                           /*trusted=*/false)
                                                  .ok;
    if (answers.size() >= 64)
        answers.clear();
    answers[planJson] = ok;
    return ok;
}

Result buildPreset(const juce::String& planJson) {
    Result result;
    const auto patch = patchPartOf(juce::JSON::parse(planJson));
    if (nodeCountOf(patch) == 0) {
        result.message = "This plan has no modules to save as a preset.";
        return result;
    }
    juce::AudioProcessorGraph scratch;
    if (const auto validation = AIStateMapper::validatePatch(patch, scratch, /*clearExisting=*/true, /*trusted=*/false);
        !validation.ok) {
        result.message = firstProblem(validation);
        return result;
    }
    prepareGraphForPatchEval(scratch);
    if (!AIStateMapper::applyJSONToGraph(patch, scratch, /*clearExisting=*/true, /*trusted=*/false)) {
        result.message = "The patch could not be built.";
        return result;
    }
    layOutUnpositioned(scratch);

    juce::var preset = AIStateMapper::graphToJSON(scratch);
    // A saved file never carries the scratch graph's node uuids: the load mints its own.
    if (auto* nodes = preset.getProperty("nodes", {}).getArray())
        for (auto& node : *nodes)
            if (auto* object = node.getDynamicObject())
                object->removeProperty("uuid");
    result.ok = true;
    result.preset = preset;
    return result;
}

juce::File fileForName(const juce::File& folder, const juce::String& rawName) {
    const auto base = SnippetManager::sanitiseName(rawName);
    if (base.isEmpty())
        return {};
    auto file = folder.getChildFile(base + ".json");
    for (int n = 2; file.exists() && n < 1000; ++n)
        file = folder.getChildFile(base + " " + juce::String(n) + ".json");
    return file;
}

Result saveAs(const juce::File& folder, const juce::String& rawName, const juce::String& planJson,
              juce::File& written) {
    written = juce::File();
    const auto file = fileForName(folder, rawName);
    if (file == juce::File{}) {
        Result result;
        result.message = "That name cannot be used for a preset file. Pick a name with letters or numbers.";
        return result;
    }
    auto result = buildPreset(planJson);
    if (!result.ok)
        return result;
    if (!folder.exists() && !folder.createDirectory().wasOk()) {
        result.ok = false;
        result.message = "The preset folder could not be created.";
        return result;
    }
    if (!file.replaceWithText(juce::JSON::toString(result.preset))) {
        result.ok = false;
        result.message = "The preset file could not be written.";
        return result;
    }
    written = file;
    return result;
}

} // namespace synth::planpreset
