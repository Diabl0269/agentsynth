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
