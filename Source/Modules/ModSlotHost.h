#pragma once

#include <functional>
#include <juce_core/juce_core.h>

namespace synth {

/** A module whose knobs take cables through modulation slots claimed on demand: a hosted plugin's parameters and a
    Drum Kit's pad knobs. A slot owns one raw input channel; a cable dropped on a knob claims a slot for that
    knob's parameter, and the slot is freed once no cable reaches its channel. Message thread. */
class ModSlotHost {
public:
    virtual ~ModSlotHost() = default;

    /** The channel that modulates `paramId`, claiming a free slot when it has none. -1 when every slot is taken or
        the parameter cannot be bound. */
    virtual int bindModSlot(const juce::String& paramId) = 0;
    /** Frees every bound slot whose channel `isConnected` says has no cable. */
    virtual void releaseUnconnectedModSlots(const std::function<bool(int rawChannel)>& isConnected) = 0;
    /** The slot channel already bound to `paramId`, or -1. Never claims one. */
    virtual int modChannelForParam(const juce::String& paramId) const = 0;
    /** True when `paramId` is bound already, or can be bound and a slot is free. */
    virtual bool canModulateParam(const juce::String& paramId) const = 0;
    /** How many parameters can be bound at once. */
    virtual int maxModSlots() const noexcept = 0;
};

} // namespace synth
