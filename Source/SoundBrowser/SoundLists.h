#pragma once

// The user's favourite sounds and the sounds used recently, as ordered path lists. Persistence is the owner's job
// (Core never touches the settings file): it round-trips each list through a settings string.

#include <functional>
#include <juce_core/juce_core.h>

namespace synth::soundbrowser {

class SoundLists {
public:
    static constexpr int kMaxRecent = 50;

    const juce::StringArray& favourites() const { return favourites_; }
    const juce::StringArray& recent() const { return recent_; }

    bool isFavourite(const juce::String& path) const { return favourites_.contains(path); }
    /** Returns the new state. */
    bool toggleFavourite(const juce::String& path);
    /** Moves `path` to the front of the recent list, which keeps the newest kMaxRecent. */
    void noteUsed(const juce::String& path);

    /** Takes `id` off both lists (a shape that was deleted); calls onChanged only when something left. */
    void forget(const juce::String& id);
    /** Puts `to` where `from` stood in each list that holds it (a shape that was renamed). */
    void replace(const juce::String& from, const juce::String& to);

    void setFavourites(const juce::StringArray& paths);
    void setRecent(const juce::StringArray& paths);

    static juce::String toSetting(const juce::StringArray& paths);
    static juce::StringArray fromSetting(const juce::String& text);

    /** Called after either list changed through toggleFavourite, noteUsed, forget or replace. */
    std::function<void()> onChanged;

private:
    juce::StringArray favourites_, recent_;
};

} // namespace synth::soundbrowser
