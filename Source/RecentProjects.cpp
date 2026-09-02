#include "RecentProjects.h"
#include <algorithm>

namespace synth {

void RecentProjects::addProject(const juce::File& bundleDir) {
    const auto path = bundleDir.getFullPathName();
    if (path.isEmpty())
        return;

    paths_.erase(std::remove(paths_.begin(), paths_.end(), path), paths_.end());
    paths_.insert(paths_.begin(), path);

    if ((int)paths_.size() > kMaxEntries)
        paths_.resize((size_t)kMaxEntries);
}

std::vector<juce::File> RecentProjects::getEntries() const {
    std::vector<juce::File> result;
    result.reserve(paths_.size());
    for (const auto& path : paths_)
        result.emplace_back(path);
    return result;
}

int RecentProjects::pruneMissing() {
    const auto before = paths_.size();
    paths_.erase(std::remove_if(paths_.begin(), paths_.end(),
                                [](const juce::String& path) { return !juce::File(path).isDirectory(); }),
                 paths_.end());
    return (int)(before - paths_.size());
}

void RecentProjects::clear() { paths_.clear(); }

std::unique_ptr<juce::XmlElement> RecentProjects::toXml() const {
    auto root = std::make_unique<juce::XmlElement>("RECENTPROJECTS");
    for (const auto& path : paths_) {
        auto* entry = root->createNewChildElement("PROJECT");
        entry->setAttribute("path", path);
    }
    return root;
}

void RecentProjects::loadFromXml(const juce::XmlElement& xml) {
    paths_.clear();
    for (auto* entry : xml.getChildIterator()) {
        if (!entry->hasTagName("PROJECT"))
            continue;
        const auto path = entry->getStringAttribute("path");
        if (path.isNotEmpty())
            paths_.push_back(path);
    }
    if ((int)paths_.size() > kMaxEntries)
        paths_.resize((size_t)kMaxEntries);
}

} // namespace synth
