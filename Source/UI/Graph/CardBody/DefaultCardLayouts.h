#pragma once

#include "Modules/CardLayout.h"
#include <map>

namespace synth {

/**
 * The hand-designed layout per module type, keyed by factory type name. `defaultRevision` is bumped
 * whenever an entry changes, so a user layout's `basedOn` can tell it is stale.
 * docs/layout/module-card-layout.md#default-layouts.
 */
class DefaultCardLayouts {
public:
    struct Entry {
        CardLayout layout;
        int defaultRevision = 1;
    };

    /** Replaces any entry for `moduleType`. */
    void add(const juce::String& moduleType, CardLayout layout, int defaultRevision);
    /** Null when the type has no code default (the automatic layout applies). */
    const Entry* find(const juce::String& moduleType) const;

    /** The registry the app uses. Currently empty: no type has a code default yet. */
    static const DefaultCardLayouts& builtIn();

private:
    std::map<juce::String, Entry> entries_;
};

} // namespace synth
