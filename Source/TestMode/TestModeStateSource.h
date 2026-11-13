#pragma once

#include <juce_core/juce_core.h>

namespace synth::testmode {

/** A component that has state worth reading from outside (the test-mode getState command adds it to the generic
 *  fields). Message thread only; must not change anything. */
class StateSource {
public:
    virtual ~StateSource() = default;
    virtual void addTestModeState(juce::DynamicObject& into) const = 0;
};

} // namespace synth::testmode
