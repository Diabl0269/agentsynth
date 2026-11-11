// ClipLaneEnterExit -- which doc changes the clip lane animates (a restore, a section edit, a repeat stamp) and the
// pictures of the clips such a change took away, shrinking toward their centre where they were. The lane owns one;
// docs/layout/undo-glide.md ("Points, dots and clips") describes the motion. Message thread only.
#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Layout/UIAnimation.h"
#include <algorithm>
#include <cstdint>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace synth::ui {

/** What one refresh of the clip lane from the doc is. */
struct ClipLaneChange {
    bool intermediate = false;                    // between the steps of one section edit: the lane keeps its last sync
    bool animates = false;                        // a restore, a section edit or a repeat stamp
    bool replaced = false;                        // the doc was replaced (a load, an undo or a redo)
    bool restored = false;                        // an undo or a redo
    std::vector<std::pair<double, double>> spans; // a stamp: only clips starting in these spans move
    bool covers(double beat) const;
};

class ClipLaneEnterExit {
public:
    /** More clips than this coming or going in one change is a different song, not an edit: they land at once. */
    static constexpr int kMaxEnterExit = 64;
    static constexpr double kExitMs = 180.0;
    static constexpr double kReducedMs = 80.0;

    using Change = ClipLaneChange;

    /** `owner` is the clip lane: it must outlive this, is repainted by the exits and supplies their clock. */
    explicit ClipLaneEnterExit(juce::Component& owner);
    ~ClipLaneEnterExit();

    /** Reads the doc's counters at a refresh (`restored` from the lane's RestoreWatch) and, unless the refresh is
     *  intermediate, remembers them for the next one. */
    Change classify(const synth::TimelineDoc& doc, bool restored);
    /** Keeps a copy of every clip and the set of tracks at this sync, for the next change's leaving clips. */
    void remember(const synth::TimelineDoc& doc);
    bool knewTrack(synth::TrackId id) const { return tracks_.count(id.value) != 0; }

    /** One leaving clip: its copy from the last sync, the row and track it was on, and where it rested. */
    struct Ghost {
        synth::Clip clip;
        int row = 0;
        synth::TrackId track;
        juce::Rectangle<int> rect;
    };
    /** The clip `id` as remembered at the last sync, or null. */
    const Ghost* remembered(std::int64_t id) const;
    /** Starts `leaving` shrinking toward their centres (Reduce Motion: a fade; Animations Off or not showing: nothing),
     *  replacing any still on screen. */
    void startExits(std::vector<Ghost> leaving);
    /** Drops the pictures of clips that are back (`alive` says which ids are in the doc). */
    template <typename Alive>
    void dropReturned(Alive&& alive) {
        std::erase_if(ghosts_, [&](const Ghost& g) { return alive(g.clip.id.value); });
    }
    void clear();

    const std::vector<Ghost>& ghosts() const noexcept { return ghosts_; }
    /** The scale (toward each picture's centre) and opacity the pictures are drawn at now. */
    float scale() const noexcept { return fade_ ? 1.0f : std::max(0.0f, 1.0f - t_); }
    float alpha() const noexcept { return fade_ ? 1.0f - t_ : 1.0f; }

    void forceAnimateForTest(bool force) noexcept { forceAnimateForTest_ = force; }
    void stepForTest(double ms) { driver_.stepForTest(ms); }
    bool isExitingForTest() const noexcept { return driver_.isRunning(); }

private:
    bool canAnimate() const;
    void repaintGhosts() const;

    juce::Component& owner_;
    std::map<std::int64_t, Ghost> clips_; // every clip at the last sync, by id (rect left empty)
    std::set<std::int64_t> tracks_;
    std::int64_t seenEdit_ = 0, seenStamp_ = 0, seenReplace_ = 0;
    bool primed_ = false;

    std::vector<Ghost> ghosts_;
    float t_ = 0.0f;
    bool fade_ = false;
    bool forceAnimateForTest_ = false;
    juce::VBlankAnimatorUpdater vblank_;
    AnimationDriver driver_;
};

} // namespace synth::ui
