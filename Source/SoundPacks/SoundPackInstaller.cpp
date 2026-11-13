#include "SoundPacks/SoundPackInstaller.h"
#include "Branding.h"
#include "Modules/Sampler/SoundPackLibrary.h"
#include "SoundPacks/SoundPackInstallWork.h"
#include <algorithm>
#include <juce_events/juce_events.h>
#include <thread>

namespace synth::soundpacks {

// The state one install shares between its worker thread and the installer; the worker only touches this, never
// the installer itself, so the installer may die while it runs.
struct InstallJob {
    std::atomic<bool> cancel{false};
    std::atomic<juce::int64> done{0};
    std::atomic<juce::int64> total{-1};
    std::atomic<int> phase{(int)InstallPhase::Downloading};
    std::atomic<bool> notifyPending{false};
    InstallOutcome outcome;
};

struct SoundPackInstaller::InstalledPack {
    juce::String id, name, version, licence, credit;
    juce::File dir;
    std::vector<IndexInstrument> instruments;
};

namespace {

constexpr juce::int64 kMaxIndexBytes = 2 * 1024 * 1024;

// True when dotted version `latest` is above `installed`; versions that are not plain numbers compare as "different".
bool isNewerVersion(const juce::String& latest, const juce::String& installed) {
    juce::StringArray a, b;
    a.addTokens(latest, ".", "");
    b.addTokens(installed, ".", "");
    const auto numeric = [](const juce::StringArray& parts) {
        for (const auto& p : parts)
            if (p.isEmpty() || !p.containsOnly("0123456789"))
                return false;
        return true;
    };
    if (!numeric(a) || !numeric(b))
        return latest != installed;
    for (int i = 0; i < juce::jmax(a.size(), b.size()); ++i) {
        const int x = i < a.size() ? a[i].getIntValue() : 0;
        const int y = i < b.size() ? b[i].getIntValue() : 0;
        if (x != y)
            return x > y;
    }
    return false;
}

// Tells the installer something changed, from any thread, without flooding the message queue: one post stays
// pending per job until it has run.
void postChange(const juce::WeakReference<SoundPackInstaller>& weak, const std::shared_ptr<InstallJob>& job,
                bool runInline, const std::function<void(SoundPackInstaller&)>& notify) {
    if (runInline) {
        if (weak != nullptr)
            notify(*weak);
        return;
    }
    if (job->notifyPending.exchange(true))
        return;
    juce::MessageManager::callAsync([weak, job, notify] {
        job->notifyPending = false;
        if (weak != nullptr)
            notify(*weak);
    });
}

} // namespace

SoundPackInstaller::SoundPackInstaller()
    : indexUrl_(branding::kSoundPackIndexUrl)
    , installRoot_(synth::sampler::SoundPackLibrary::userRoot())
    , fetcher_(makeHttpFetcher())
    , fileOpener_([](const juce::File& file) { file.startAsProcess(); }) {}

SoundPackInstaller::~SoundPackInstaller() {
    for (auto& [id, job] : jobs_)
        job->cancel = true;
}

void SoundPackInstaller::notifyChanged() {
    listeners_.call([](Listener& l) { l.soundPacksChanged(); });
}

void SoundPackInstaller::run(std::function<void()> work, std::function<void()> deliver) {
    if (runInline_) {
        work();
        deliver();
        return;
    }
    std::thread([work = std::move(work), deliver = std::move(deliver)]() mutable {
        work();
        juce::MessageManager::callAsync(std::move(deliver));
    }).detach();
}

// ---- The hosted list ----

void SoundPackInstaller::refreshIndex() {
    if (indexStatus_ == IndexStatus::Loading)
        return;
    indexStatus_ = IndexStatus::Loading;
    indexError_ = {};
    notifyChanged();

    struct Fetched {
        bool ok = false;
        SoundPackIndex index;
        juce::String error;
    };
    auto fetched = std::make_shared<Fetched>();
    const juce::WeakReference<SoundPackInstaller> weak(this);
    run(
        [fetched, url = indexUrl_, fetcher = fetcher_] {
            const juce::File temp = juce::File::createTempFile(".json");
            const std::atomic<bool> never{false};
            const FetchResult got = fetcher(url, temp, [](juce::int64, juce::int64) {}, never);
            if (!got.ok || temp.getSize() > kMaxIndexBytes) {
                fetched->error = "Couldn't reach the pack list.";
            } else {
                auto parsed = SoundPackIndex::parse(temp.loadFileAsString());
                fetched->ok = parsed.ok;
                fetched->index = std::move(parsed.index);
                fetched->error = parsed.error;
            }
            temp.deleteFile();
        },
        [weak, fetched] {
            if (weak != nullptr)
                weak->applyIndex(fetched->ok, std::move(fetched->index), fetched->error);
        });
}

void SoundPackInstaller::applyIndex(bool ok, SoundPackIndex index, const juce::String& error) {
    indexStatus_ = ok ? IndexStatus::Loaded : IndexStatus::Failed;
    indexError_ = ok ? juce::String() : error;
    if (ok)
        index_ = std::move(index);
    notifyChanged();
}

// ---- What the UI shows ----

std::vector<SoundPackInstaller::InstalledPack> SoundPackInstaller::scanInstalled() const {
    std::vector<InstalledPack> found;
    for (const auto& root : synth::sampler::SoundPackLibrary::roots()) {
        auto folders = root.findChildFiles(juce::File::findDirectories, false);
        folders.sort();
        for (const auto& folder : folders) {
            const juce::var manifest = juce::JSON::parse(folder.getChildFile("pack.json"));
            InstalledPack pack;
            pack.id = manifest.getProperty("id", {}).toString();
            const bool seen = std::any_of(found.begin(), found.end(), [&](const auto& p) { return p.id == pack.id; });
            if (!manifest.isObject() || pack.id.isEmpty() || seen)
                continue;
            pack.name = manifest.getProperty("name", {}).toString().trim();
            pack.version = manifest.getProperty("version", {}).toString().trim();
            pack.licence = manifest.getProperty("licence", {}).toString();
            pack.credit = manifest.getProperty("credit", {}).toString();
            pack.dir = folder;
            if (const auto* list = manifest.getProperty("instruments", {}).getArray())
                for (const auto& entry : *list)
                    pack.instruments.push_back({entry.getProperty("id", {}).toString(),
                                                entry.getProperty("name", {}).toString(),
                                                entry.getProperty("category", {}).toString()});
            found.push_back(std::move(pack));
        }
    }
    return found;
}

SoundPackInstaller::Row SoundPackInstaller::makeRow(const IndexPack* hosted, const InstalledPack* installed) const {
    Row row;
    row.id = hosted != nullptr ? hosted->id : installed->id;
    row.name = hosted != nullptr ? hosted->name : (installed->name.isNotEmpty() ? installed->name : installed->id);
    row.inIndex = hosted != nullptr;
    if (hosted != nullptr) {
        row.latestVersion = hosted->version;
        row.licence = hosted->licence;
        row.licenceUrl = hosted->licenceUrl;
        row.credit = hosted->credit;
        row.downloadBytes = hosted->downloadBytes;
        row.installedBytes = hosted->installedBytes;
        row.instruments = hosted->instruments;
    }
    if (installed != nullptr) {
        row.installedVersion = installed->version.isNotEmpty() ? installed->version : juce::String("1");
        row.removable = installed->dir.getParentDirectory() == installRoot_;
        const juce::File sources = installed->dir.getChildFile("LICENSES/SOURCES.md");
        if (sources.existsAsFile())
            row.sources = sources;
        if (hosted == nullptr) {
            row.licence = installed->licence;
            row.credit = installed->credit;
            row.instruments = installed->instruments;
        }
        row.state = hosted != nullptr && isNewerVersion(hosted->version, row.installedVersion) ? State::UpdateAvailable
                                                                                               : State::Installed;
    }
    if (const auto job = jobs_.find(row.id); job != jobs_.end()) {
        const juce::int64 total = job->second->total.load();
        row.progress = total > 0 ? (float)((double)job->second->done.load() / (double)total) : -1.0f;
        switch ((InstallPhase)job->second->phase.load()) {
        case InstallPhase::Downloading:
            row.state = State::Downloading;
            break;
        case InstallPhase::Verifying:
            row.state = State::Verifying;
            break;
        case InstallPhase::Installing:
            row.state = State::Installing;
            break;
        }
    }
    if (const auto err = errors_.find(row.id); err != errors_.end())
        row.error = err->second;
    return row;
}

std::vector<SoundPackInstaller::Row> SoundPackInstaller::rows() const {
    const auto installed = scanInstalled();
    const auto installedFor = [&installed](const juce::String& id) -> const InstalledPack* {
        for (const auto& pack : installed)
            if (pack.id == id)
                return &pack;
        return nullptr;
    };
    std::vector<Row> out;
    for (const auto& hosted : index_.packs)
        out.push_back(makeRow(&hosted, installedFor(hosted.id)));
    for (const auto& pack : installed)
        if (index_.find(pack.id) == nullptr)
            out.push_back(makeRow(nullptr, &pack));
    return out;
}

std::optional<SoundPackInstaller::Row> SoundPackInstaller::row(const juce::String& packId) const {
    for (auto& r : rows())
        if (r.id == packId)
            return std::move(r);
    return std::nullopt;
}

// ---- Install, cancel, remove ----

bool SoundPackInstaller::install(const juce::String& packId) {
    const IndexPack* hosted = index_.find(packId);
    if (hosted == nullptr || jobs_.count(packId) != 0)
        return false;
    errors_.erase(packId);
    auto job = std::make_shared<InstallJob>();
    jobs_[packId] = job;

    const juce::WeakReference<SoundPackInstaller> weak(this);
    const bool inlineRun = runInline_;
    const auto notify = [](SoundPackInstaller& self) { self.notifyChanged(); };
    InstallRequest request{*hosted, installRoot_, fetcher_};
    InstallHooks hooks;
    hooks.cancel = &job->cancel;
    hooks.progress = [weak, job, inlineRun, notify](juce::int64 done, juce::int64 total) {
        job->done = done;
        job->total = total;
        postChange(weak, job, inlineRun, notify);
    };
    hooks.phase = [weak, job, inlineRun, notify](InstallPhase phase) {
        job->phase = (int)phase;
        postChange(weak, job, inlineRun, notify);
    };
    notifyChanged();
    run([request, hooks, job] { job->outcome = runInstall(request, hooks); },
        [weak, job, packId] {
            if (weak != nullptr)
                weak->finishJob(packId, job);
        });
    return true;
}

void SoundPackInstaller::finishJob(const juce::String& packId, const std::shared_ptr<InstallJob>& job) {
    jobs_.erase(packId);
    if (!job->outcome.ok && !job->outcome.cancelled)
        errors_[packId] = job->outcome.error;
    notifyChanged();
}

void SoundPackInstaller::cancel(const juce::String& packId) {
    if (const auto job = jobs_.find(packId); job != jobs_.end())
        job->second->cancel = true;
}

bool SoundPackInstaller::remove(const juce::String& packId) {
    const auto current = row(packId);
    if (!current || current->installedVersion.isEmpty() || jobs_.count(packId) != 0)
        return false;
    juce::File dir;
    for (const auto& pack : scanInstalled())
        if (pack.id == packId)
            dir = pack.dir;
    if (!current->removable || !dir.isDirectory()) {
        errors_[packId] = "This pack is part of the app and can't be removed.";
    } else if (!dir.deleteRecursively()) {
        errors_[packId] = "The pack could not be removed. Close anything using its files and try again.";
    } else {
        errors_.erase(packId);
        notifyChanged();
        return true;
    }
    notifyChanged();
    return false;
}

void SoundPackInstaller::showSources(const juce::String& packId) {
    const auto current = row(packId);
    if (current && current->sources != juce::File() && fileOpener_)
        fileOpener_(current->sources);
}

} // namespace synth::soundpacks
