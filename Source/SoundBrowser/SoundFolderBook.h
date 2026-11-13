#pragma once

// The user's own sound folders: an ordered list with a display name each, kept in step with the sound index. Core never
// touches the settings file, so the owner round-trips the list through two settings strings and saves on a change.

#include "SoundBrowser/SoundIndex.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace synth::soundbrowser {

struct UserFolder {
    juce::File path;
    juce::String name; ///< What the user called it; empty means the folder's own name.

    juce::String displayName() const { return name.isNotEmpty() ? name : path.getFileName(); }
};

class SoundFolderBook {
public:
    /** Called on the message thread after the list or a name changed. */
    class Listener {
    public:
        virtual ~Listener() = default;
        virtual void soundFoldersChanged() = 0;
    };

    explicit SoundFolderBook(SoundIndex& index)
        : index_(index) {}

    void addListener(Listener* l) { listeners_.add(l); }
    void removeListener(Listener* l) { listeners_.remove(l); }

    /** Replaces the list from its two settings strings (see `pathsSetting`, `namesSetting`); tells nobody. */
    void load(const juce::String& pathsSetting, const juce::String& namesSetting);
    /** Hands the list to the index, which scans it; before this the index knows no folders (the owner starts it). */
    void goLive();

    const std::vector<UserFolder>& folders() const { return folders_; }
    const UserFolder* find(const juce::File& path) const;

    /** Adds an existing folder; false for a file, a path that is not there or one already listed. Scans it. */
    bool add(const juce::File& path);
    /** Forgets the folder. Nothing on disk is touched. */
    void remove(const juce::File& path);
    /** Sets the display name only; an empty name, or the folder's own, restores the default. */
    void rename(const juce::File& path, const juce::String& name);

    juce::String pathsSetting() const;
    juce::String namesSetting() const;

private:
    void pushToIndex();
    void changed();

    SoundIndex& index_;
    std::vector<UserFolder> folders_;
    bool live_ = false;
    juce::ListenerList<Listener> listeners_;
};

} // namespace synth::soundbrowser
