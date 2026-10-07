#pragma once

// AppUndoManagerSnapshotSize.h -- how AppUndoManager's snapshot steps size themselves for the undo history (private to
// the AppUndoManager*.cpp units).

#include <initializer_list>
#include <juce_core/juce_core.h>
#include <utility>
#include <vector>

namespace undo_size {

/** One record call's change check: compares states as JSON text and keeps each text's length, so the steps that call
 *  then pushes are sized without writing the same snapshots out again. Message thread only; nests. */
class MeasuredJson {
public:
    MeasuredJson();
    ~MeasuredJson();
    /** True when `a` and `b` write out as different JSON. */
    bool differs(const juce::var& a, const juce::var& b);
    /** The length the innermost live check measured for `state`, or -1. */
    static int lengthOf(const juce::var& state);

private:
    int measure(const juce::var& state, const juce::String& text);
    MeasuredJson* previous_;
    std::vector<std::pair<const void*, int>> lengths_;
};

/** `memo`, filled on first use with the states' total JSON length. */
int sizedOnce(int& memo, std::initializer_list<const juce::var*> states);

} // namespace undo_size
