#include "SoundBrowser/SoundLists.h"

namespace synth::soundbrowser {

bool SoundLists::toggleFavourite(const juce::String& path) {
    const bool nowFavourite = !favourites_.contains(path);
    if (nowFavourite)
        favourites_.add(path);
    else
        favourites_.removeString(path);
    if (onChanged)
        onChanged();
    return nowFavourite;
}

void SoundLists::noteUsed(const juce::String& path) {
    recent_.removeString(path);
    recent_.insert(0, path);
    while (recent_.size() > kMaxRecent)
        recent_.remove(recent_.size() - 1);
    if (onChanged)
        onChanged();
}

void SoundLists::forget(const juce::String& id) {
    const bool inFavourites = favourites_.contains(id);
    const bool inRecent = recent_.contains(id);
    favourites_.removeString(id);
    recent_.removeString(id);
    if ((inFavourites || inRecent) && onChanged)
        onChanged();
}

void SoundLists::replace(const juce::String& from, const juce::String& to) {
    bool changed = false;
    for (auto* list : {&favourites_, &recent_}) {
        const int at = list->indexOf(from);
        if (at < 0)
            continue;
        if (list->contains(to))
            list->remove(at);
        else
            list->set(at, to);
        changed = true;
    }
    if (changed && onChanged)
        onChanged();
}

void SoundLists::setFavourites(const juce::StringArray& paths) { favourites_ = paths; }

void SoundLists::setRecent(const juce::StringArray& paths) {
    recent_ = paths;
    while (recent_.size() > kMaxRecent)
        recent_.remove(recent_.size() - 1);
}

juce::String SoundLists::toSetting(const juce::StringArray& paths) { return paths.joinIntoString("\n"); }

juce::StringArray SoundLists::fromSetting(const juce::String& text) {
    juce::StringArray paths;
    paths.addLines(text);
    paths.removeEmptyStrings();
    return paths;
}

} // namespace synth::soundbrowser
