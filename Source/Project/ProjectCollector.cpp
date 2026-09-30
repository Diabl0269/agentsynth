// ProjectCollector.cpp — plan/copy/apply for Collect (external module files into the bundle) and
// the zip writer for Archive.
#include "ProjectCollector.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "Modules/ModuleBase.h"
#include "Modules/WavetableOscillatorModule/WavetableOscillatorModule.h"
#include "Project/ModuleFileRefs.h"
#include "ProjectBundle.h"
#include "Timeline/AssetManager.h"
#include <algorithm>
#include <set>

namespace synth {

namespace {

// A live module's file-bearing extra state. getExtraState() returns a fresh object every call, so
// `state` can be edited and handed straight back to setExtraState.
struct ModuleState {
    ModuleBase* module;
    juce::String type;
    juce::var state;
};

std::vector<ModuleState> moduleStates(juce::AudioProcessorGraph& graph) {
    std::vector<ModuleState> states;
    for (auto* node : graph.getNodes()) {
        auto* module = dynamic_cast<ModuleBase*>(node->getProcessor());
        if (module == nullptr)
            continue;
        auto state = module->getExtraState();
        if (state.getDynamicObject() != nullptr)
            states.push_back({module, AIStateMapper::getFactoryTypeName(node->getProcessor()), state});
    }
    return states;
}

// Builds a CollectPlan, deduplicating by source path so a sample shared by two Samplers is copied
// once, and giving each collected folder its own collision-free directory under Wavetables/.
class PlanBuilder {
public:
    explicit PlanBuilder(const juce::File& bundle)
        : bundleDir(bundle) {}

    // A value Collect has nothing to do for: already inside the bundle, or not a real path.
    bool isInternalOrInvalid(const juce::String& value) const {
        return !juce::File::isAbsolutePath(value) || juce::File(value).isAChildOf(bundleDir);
    }

    // The whole folder, non-recursive, because the Wavetable's < > browser steps through it.
    void addFolder(const juce::File& folder, const char* subdir) {
        if (folderDests.count(folder.getFullPathName()) != 0)
            return;
        if (!folder.isDirectory()) {
            plan.missing.addIfNotAlreadyThere(folder.getFullPathName());
            return;
        }
        const auto dest = uniqueFolderDest(bundleDir.getChildFile(subdir), folder.getFileName());
        folderDests[folder.getFullPathName()] = dest;
        plan.folders.emplace_back(folder, dest);
        for (const auto& entry : juce::RangedDirectoryIterator(folder, false, "*", juce::File::findFiles))
            if (WavetableOscillatorModule::isSupportedWavetableFile(entry.getFile()))
                addFile(entry.getFile(), dest);
    }

    // A file inside a folder being collected lands in that folder's copy, so the browser cursor
    // still finds it; anything else goes to `defaultDir`.
    void addFileValue(const juce::File& file, const juce::File& defaultDir) {
        const auto folderIt = folderDests.find(file.getParentDirectory().getFullPathName());
        addFile(file, folderIt != folderDests.end() ? folderIt->second : defaultDir);
    }

    CollectPlan plan;
    const juce::File bundleDir;

private:
    void addFile(const juce::File& file, const juce::File& destDir) {
        if (!file.existsAsFile()) {
            plan.missing.addIfNotAlreadyThere(file.getFullPathName());
            return;
        }
        if (queued.insert(file.getFullPathName()).second)
            plan.copies.push_back({file, destDir});
    }

    juce::File uniqueFolderDest(const juce::File& parent, const juce::String& name) {
        for (int n = 1;; ++n) {
            const auto candidate = parent.getChildFile(n == 1 ? name : name + "-" + juce::String(n));
            if (!candidate.exists() && claimed.insert(candidate.getFullPathName()).second)
                return candidate;
        }
    }

    std::set<juce::String> queued;
    std::set<juce::String> claimed;
    std::map<juce::String, juce::File> folderDests;
};

bool shouldArchive(const juce::File& file, const juce::File& bundleDir, const juce::File& zipFile,
                   const juce::StringArray& excludedDirs) {
    if (file == zipFile || file.getFileName().startsWithChar('.'))
        return false;
    const auto relative = file.getRelativePathFrom(bundleDir).replaceCharacter('\\', '/');
    const bool atRoot = !relative.containsChar('/');
    if (atRoot && file.getFileName().startsWith("autosave") && file.hasFileExtension("json"))
        return false; // the live sidecar and its numbered backups: a recipient must not get a recovery prompt
    return atRoot || !excludedDirs.contains(relative.upToFirstOccurrenceOf("/", false, false));
}

} // namespace

// Folders are planned before files so a Wavetable's loaded table can be routed into its folder's
// copy (see PlanBuilder::addFileValue).
CollectPlan ProjectCollector::plan(juce::AudioProcessorGraph& graph, const juce::File& bundleDir) {
    PlanBuilder builder(bundleDir);
    const auto states = moduleStates(graph);
    for (const bool folders : {true, false})
        for (const auto& ms : states)
            ModuleFileRefs::forEachFileValue(ms.type, ms.state, [&](const ModuleFileKey& key, const juce::String& v) {
                if (key.isFolder != folders || builder.isInternalOrInvalid(v))
                    return;
                if (folders)
                    builder.addFolder(juce::File(v), key.bundleSubdir);
                else
                    builder.addFileValue(juce::File(v), bundleDir.getChildFile(key.bundleSubdir));
            });
    return std::move(builder.plan);
}

// importAudioFileToDirectory supplies the rules: the source must read as audio, the name is
// collision-free, and an identical file already there is reused rather than copied twice. A
// cancel stops before the next copy and records no folder rewrites; files already copied stay
// (Collect never deletes) and the caller applies nothing.
CollectResult ProjectCollector::copy(const CollectPlan& plan, const ProgressFn& progress) {
    CollectResult result;
    result.missing = plan.missing;
    const auto total = (double)std::max<size_t>(1, plan.copies.size());
    for (size_t i = 0; i < plan.copies.size(); ++i) {
        if (progress && !progress((double)i / total)) {
            result.cancelled = true;
            return result;
        }
        const auto& job = plan.copies[i];
        juce::String error;
        const auto name = AssetManager::importAudioFileToDirectory(job.source, job.destDir, &error);
        if (name.isEmpty()) {
            result.failures.add(job.source.getFullPathName() + ": " + error);
            continue;
        }
        result.rewrites[job.source.getFullPathName()] = job.destDir.getChildFile(name).getFullPathName();
        ++result.filesCopied;
    }
    for (const auto& [source, dest] : plan.folders)
        if (dest.createDirectory().wasOk())
            result.rewrites[source.getFullPathName()] = dest.getFullPathName();
    if (progress)
        progress(1.0);
    return result;
}

int ProjectCollector::apply(juce::AudioProcessorGraph& graph, const CollectResult& result) {
    int changed = 0;
    for (auto& ms : moduleStates(graph)) {
        bool touched = false;
        ModuleFileRefs::forEachFileValue(ms.type, ms.state, [&](const ModuleFileKey& key, const juce::String& v) {
            const auto it = result.rewrites.find(v);
            if (it == result.rewrites.end())
                return;
            ms.state.getDynamicObject()->setProperty(key.stateKey, it->second);
            touched = true;
        });
        if (touched) {
            ms.module->setExtraState(ms.state);
            ++changed;
        }
    }
    return changed;
}

CollectResult ProjectCollector::collect(juce::AudioProcessorGraph& graph, const juce::File& bundleDir) {
    const auto collectPlan = plan(graph, bundleDir);
    auto result = copy(collectPlan, nullptr);
    result.missing = collectPlan.missing;
    apply(graph, result);
    return result;
}

// Written to a temporary sibling and swapped in, so a failed or partial write never leaves a
// truncated .zip where the user asked for one.
juce::String ProjectCollector::writeArchive(const juce::File& bundleDir, const juce::File& zipFile,
                                            const juce::StringArray& excludedDirs, double* progress) {
    if (!bundleDir.isDirectory())
        return "\"" + bundleDir.getFullPathName() + "\" is not a project folder.";

    juce::ZipFile::Builder builder;
    const auto root = bundleDir.getFileName();
    for (const auto& file : bundleDir.findChildFiles(juce::File::findFiles, true))
        if (shouldArchive(file, bundleDir, zipFile, excludedDirs))
            builder.addFile(file, 6, root + "/" + file.getRelativePathFrom(bundleDir).replaceCharacter('\\', '/'));

    juce::TemporaryFile temp(zipFile);
    {
        std::unique_ptr<juce::FileOutputStream> out(temp.getFile().createOutputStream());
        if (out == nullptr || out->failedToOpen() || !builder.writeToStream(*out, progress))
            return "Could not write \"" + zipFile.getFullPathName() + "\".";
        out->flush();
    }
    if (!temp.overwriteTargetFileWithTemporary())
        return "Could not replace \"" + zipFile.getFullPathName() + "\".";
    return {};
}

} // namespace synth
