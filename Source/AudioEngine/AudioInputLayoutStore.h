#pragma once

#include <functional>
#include <juce_core/juce_core.h>
#include <map>
#include <set>
#include <vector>

// The audio inputs of every interface the app has ever seen, as the user laid them out: which neighbouring jacks
// (1-2, 3-4, ...) are joined into one stereo input, and an optional name per input. Pure data keyed by device type
// plus device name (docs/layout/settings-preferences.md#the-audio-tab); persisting it is the owner's job (`onChange`),
// the engine never touches user settings.
//
// An entry leaves the store only through forget(): switching device, unplugging, sleeping or the saved device state
// being cleaned up never touch it, so a replugged interface finds its names and pairs as it left them.
namespace synth {

// One input a track can pick on the current interface: a mono jack or a paired stereo input.
struct InputSlot {
    int firstJack = 0; // 0-based
    int numJacks = 1;  // 1 or 2
    juce::String name; // empty = unnamed
    bool operator==(const InputSlot& other) const noexcept {
        return firstJack == other.firstJack && numJacks == other.numJacks && name == other.name;
    }
};

class AudioInputLayoutStore {
public:
    static constexpr int kMaxJacks = 8;
    static constexpr int kMaxPairs = kMaxJacks / 2;

    struct Entry {
        juce::String deviceType;
        juce::String deviceName;
        std::set<int> pairedPairs;         // pair p = jacks 2p and 2p+1
        std::map<int, juce::String> names; // by the input's first jack
        juce::int64 lastUsedMs = 0;
    };

    struct Listener {
        virtual ~Listener() = default;
        virtual void inputLayoutChanged() = 0;
    };

    /** Called after every change, for the owner to save toJson(). */
    std::function<void()> onChange;
    void addListener(Listener* listener);
    void removeListener(Listener* listener);

    /** Marks the interface as used now; a first sighting gets an entry with no pairs and no names. */
    void touch(const juce::String& deviceType, const juce::String& deviceName,
               juce::int64 nowMs = juce::Time::currentTimeMillis());
    bool has(const juce::String& deviceType, const juce::String& deviceName) const;

    bool isPaired(const juce::String& deviceType, const juce::String& deviceName, int pair) const;
    void setPaired(const juce::String& deviceType, const juce::String& deviceName, int pair, bool paired);
    /** `firstJack` is the mono jack, or the first jack of a paired input. An empty name clears it. */
    juce::String getName(const juce::String& deviceType, const juce::String& deviceName, int firstJack) const;
    void setName(const juce::String& deviceType, const juce::String& deviceName, int firstJack,
                 const juce::String& name);
    /** Removes only this interface; false when it was not there. */
    bool forget(const juce::String& deviceType, const juce::String& deviceName);

    /** How many of this interface's inputs carry a name (a paired input counts once). */
    int countNamedInputs(const juce::String& deviceType, const juce::String& deviceName, int numJacks) const;
    /** The interface's inputs in jack order for `numJacks` jacks (at most kMaxJacks). */
    std::vector<InputSlot> slotsFor(const juce::String& deviceType, const juce::String& deviceName, int numJacks) const;
    /** Most recently used first. */
    std::vector<Entry> entries() const;

    juce::String toJson() const;
    /** Replaces the store with `json`; anything malformed leaves it empty. Does not call onChange. */
    void loadJson(const juce::String& json);

private:
    using Key = std::pair<juce::String, juce::String>;
    Entry& entryFor(const juce::String& deviceType, const juce::String& deviceName);
    const Entry* find(const juce::String& deviceType, const juce::String& deviceName) const;
    void changed();

    std::map<Key, Entry> entries_;
    juce::ListenerList<Listener> listeners_;
};

} // namespace synth
