#include "SoundBrowser/SoundFolderBook.h"
#include "SoundBrowser/SoundLists.h"
#include <algorithm>
#include <map>

namespace synth::soundbrowser {

namespace {
std::vector<UserFolder>::iterator findFolder(std::vector<UserFolder>& folders, const juce::File& path) {
    return std::find_if(folders.begin(), folders.end(), [&](const UserFolder& f) { return f.path == path; });
}
} // namespace

const UserFolder* SoundFolderBook::find(const juce::File& path) const {
    for (const auto& f : folders_)
        if (f.path == path)
            return &f;
    return nullptr;
}

void SoundFolderBook::load(const juce::String& pathsSetting, const juce::String& namesSetting) {
    folders_.clear();
    const auto names = juce::JSON::parse(namesSetting);
    for (const auto& line : SoundLists::fromSetting(pathsSetting)) {
        const juce::File path(line);
        if (find(path) == nullptr)
            folders_.push_back({path, names.getProperty(line, {}).toString()});
    }
}

void SoundFolderBook::goLive() {
    live_ = true;
    pushToIndex();
}

// The list goes first: setUserFolders starts a scan of the new folders, and the names must be known when it publishes.
void SoundFolderBook::pushToIndex() {
    if (!live_)
        return;
    std::map<juce::String, juce::String> names;
    std::vector<juce::File> paths;
    for (const auto& f : folders_) {
        paths.push_back(f.path);
        if (f.name.isNotEmpty())
            names[f.path.getFullPathName()] = f.name;
    }
    index_.setUserFolderNames(names);
    index_.setUserFolders(paths);
}

void SoundFolderBook::changed() {
    pushToIndex();
    listeners_.call([](Listener& l) { l.soundFoldersChanged(); });
}

bool SoundFolderBook::add(const juce::File& path) {
    if (!path.isDirectory() || find(path) != nullptr)
        return false;
    folders_.push_back({path, {}});
    changed();
    return true;
}

void SoundFolderBook::remove(const juce::File& path) {
    const auto it = findFolder(folders_, path);
    if (it == folders_.end())
        return;
    folders_.erase(it);
    changed();
}

void SoundFolderBook::rename(const juce::File& path, const juce::String& name) {
    const auto it = findFolder(folders_, path);
    if (it == folders_.end())
        return;
    const auto trimmed = name.trim();
    const auto next = trimmed == path.getFileName() ? juce::String() : trimmed;
    if (it->name == next)
        return;
    it->name = next;
    changed();
}

juce::String SoundFolderBook::pathsSetting() const {
    juce::StringArray paths;
    for (const auto& f : folders_)
        paths.add(f.path.getFullPathName());
    return SoundLists::toSetting(paths);
}

juce::String SoundFolderBook::namesSetting() const {
    auto* names = new juce::DynamicObject();
    bool any = false;
    for (const auto& f : folders_)
        if (f.name.isNotEmpty()) {
            names->setProperty(juce::Identifier(f.path.getFullPathName()), f.name);
            any = true;
        }
    if (!any) {
        delete names;
        return {};
    }
    return juce::JSON::toString(juce::var(names), true);
}

} // namespace synth::soundbrowser
