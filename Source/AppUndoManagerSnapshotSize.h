#pragma once

// AppUndoManagerSnapshotSize.h -- how AppUndoManager's snapshot steps size themselves for the undo history (private to
// the AppUndoManager*.cpp units).

#include <functional>
#include <initializer_list>
#include <juce_core/juce_core.h>

namespace undo_size {

/** While alive, the steps pushed take the size of any object or array `lookup` knows (not -1) from it instead of
 *  counting it again. Message thread only; nests. */
class KnownSizes {
public:
    explicit KnownSizes(std::function<int(const void*)> lookup);
    ~KnownSizes();
    KnownSizes(const KnownSizes&) = delete;
    KnownSizes& operator=(const KnownSizes&) = delete;

private:
    friend int sizedOnce(int&, std::initializer_list<const juce::var*>);
    std::function<int(const void*)> lookup_;
    KnownSizes* previous_;
};

/** `memo`, filled on first use with the states' total synth::estimateJsonSize. */
int sizedOnce(int& memo, std::initializer_list<const juce::var*> states);

} // namespace undo_size
