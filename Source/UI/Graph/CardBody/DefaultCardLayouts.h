#pragma once

#include "Modules/CardLayout.h"
#include <functional>
#include <map>
#include <vector>

namespace synth {

/**
 * A dim rule a stored layout cannot express (a numeric test, such as Detune while Unison is 1):
 * `paramId`'s control greys out while `dims` returns true, on any layout of the type.
 */
struct CardDimRule {
    juce::String paramId;
    juce::StringArray watched; ///< The parameter ids whose change re-evaluates `dims`.
    std::function<bool(juce::AudioProcessor& module)> dims;
};

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
        std::vector<CardDimRule> dimRules; ///< Applied to any layout of this type.
    };

    /** Replaces any entry for `moduleType`. */
    void add(const juce::String& moduleType, CardLayout layout, int defaultRevision,
             std::vector<CardDimRule> dimRules = {});
    /** Null when the type has no code default (the automatic layout applies). */
    const Entry* find(const juce::String& moduleType) const;

    /** The registry the app uses: every module family's registration function, called once. */
    static const DefaultCardLayouts& builtIn();

private:
    std::map<juce::String, Entry> entries_;
};

} // namespace synth
