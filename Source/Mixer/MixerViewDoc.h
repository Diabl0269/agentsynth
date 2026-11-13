#pragma once

#include <juce_core/juce_core.h>
#include <map>
#include <set>
#include <vector>

namespace synth {

/** Where a mixer channel's column sits: pinned at the left, in the scrolling middle, pinned right. */
enum class MixerZone { Left, Scrolling, Right };

/** "left" / "scrolling" / "right", the spelling the project file uses. */
const char* mixerZoneToString(MixerZone zone) noexcept;
/** An unrecognised name reads as Scrolling. */
MixerZone mixerZoneFromString(const juce::String& text) noexcept;

/**
 * @class MixerViewDoc
 * @brief The project's mixer view state: which channels are pinned left or right, which are hidden and the
 *        order of the buses (docs/mixer/panel.md#side-pane-zones-and-visibility).
 *
 * Channels are keyed by a stable id: a strip's or bus's node `uuid` (which survives save and load, unlike a
 * NodeID), or the fixed ids `kMasterId` / `kDirectId` for the two columns that have no uuid of their own.
 * A channel with no entry is Scrolling and shown. Pure data, headless, copyable: the owner records edits
 * through `AppUndoManager::recordMixerViewChange` and rebuilds the mixer afterwards.
 *
 * Persisted as the project's top-level `"mixerView"` key (`toVar` / `fromVar`, all-or-nothing).
 */
class MixerViewDoc {
public:
    static constexpr const char* kMasterId = "master";
    static constexpr const char* kDirectId = "direct";

    /** The layout a brand-new project starts with: Master pinned right. A document with no entries
     *  (a project saved before this existed, or a file with no key) leaves every column scrolling. */
    static MixerViewDoc forNewProject();

    MixerZone getZone(const juce::String& channelId) const;
    /** Scrolling removes the entry. */
    void setZone(const juce::String& channelId, MixerZone zone);

    bool isHidden(const juce::String& channelId) const;
    /** Master cannot be hidden: hiding it is ignored. */
    void setHidden(const juce::String& channelId, bool hidden);
    int getHiddenCount() const noexcept { return static_cast<int>(hidden_.size()); }
    void showAll() { hidden_.clear(); }
    /** The hidden ids in id order. */
    std::vector<juce::String> getHiddenIds() const { return {hidden_.begin(), hidden_.end()}; }

    /** The saved order of the track-less (bus) columns; ids not listed sort after the listed ones. */
    const std::vector<juce::String>& getBusOrder() const noexcept { return busOrder_; }
    /** Empty and duplicate ids are dropped. */
    void setBusOrder(std::vector<juce::String> order);
    /** `currentIds` sorted so saved ids come first in saved order, then the unknown ones in input order. */
    std::vector<juce::String> orderBuses(const std::vector<juce::String>& currentIds) const;

    /** Drops every entry whose id is not in `aliveIds`, keeping the two fixed ids. */
    void retainOnly(const std::vector<juce::String>& aliveIds);

    bool isEmpty() const noexcept { return zones_.empty() && hidden_.empty() && busOrder_.empty(); }
    bool operator==(const MixerViewDoc& other) const {
        return zones_ == other.zones_ && hidden_ == other.hidden_ && busOrder_ == other.busOrder_;
    }
    bool operator!=(const MixerViewDoc& other) const { return !(*this == other); }

    juce::var toVar() const;
    /** All-or-nothing: on failure the document is left untouched. */
    bool fromVar(const juce::var& v);

private:
    std::map<juce::String, MixerZone> zones_; // never holds Scrolling
    std::set<juce::String> hidden_;
    std::vector<juce::String> busOrder_;
};

} // namespace synth
