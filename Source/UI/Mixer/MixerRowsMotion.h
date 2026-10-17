#pragma once

#include "Mixer/MixerModel/MixerModel.h"
#include "UI/Layout/ExitEnterList/ExitEnterListMotion.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

// MixerRowsMotion.h (docs/layout/animation.md "Delete and undo animation"): the motion of a column's insert and send
// rows when one is added or removed. The lists are rebuilt as new objects on every edit, so the panel takes a Snapshot
// of the old column (rows plus, for a removal, a picture of the section) and the new column plays it once its lists
// are laid out. A removed row shrinks away and the rows below close the gap; an added row opens the gap, grows in and
// fades an outline. Rows that only change in place, or that both come and go, land at once.
//
// Owned by the column. The owner decides whether to animate at all (on screen, or forced by a test) and only calls
// play() then; headless runs never get here, tests step each motion's applyAtMs() by hand.
namespace synth::ui {

class MixerRowsMotion {
public:
    enum class List { Inserts, Sends };

    /** One list's rows in its section's frame (the viewport's coordinates, in the host's). */
    struct Part {
        std::vector<ExitEnterListRow> rows;
        juce::Rectangle<int> area; // the section frame in the host's coordinates; empty when nothing can move
        juce::Image picture;       // of `area`; only taken when the next state drops rows and adds none
        float pictureScale = 1.0f;
    };
    struct Snapshot {
        Part inserts, sends;
    };

    explicit MixerRowsMotion(juce::Component& host);

    /** Row keys: a module row by its uuid, a split row by its fork's uuid, a send row by its slot. */
    static juce::String insertKey(const synth::MixerInsertEntry& entry);
    static juce::String sendKey(const synth::MixerSendEntry& entry);

    /** `listRows` are in the list's coordinates. Empty (nothing moves) when the frame is hidden, empty or scrolled. */
    static Part rowsIn(const juce::Viewport& frame, std::vector<ExitEnterListRow> listRows);
    /** rowsIn() plus a picture of the frame when `nextKeys` drops some of `listRows` and adds none. */
    static Part capture(juce::Component& host, const juce::Viewport& frame, std::vector<ExitEnterListRow> listRows,
                        const std::vector<juce::String>& nextKeys);

    /** Plays the change from `before` to `after` in `list`; false when it has nothing to show or lands at once. */
    bool play(List list, const Part& before, const Part& after, juce::Colour background);
    void finishNow();

    ExitEnterListMotion& motionFor(List list) noexcept { return list == List::Inserts ? inserts_ : sends_; }

private:
    juce::Component& host_;
    ExitEnterListMotion inserts_;
    ExitEnterListMotion sends_;
};

} // namespace synth::ui
