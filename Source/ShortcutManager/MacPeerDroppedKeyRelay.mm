// Concern: the Cocoa half of MacPeerDroppedKey.h: a local keyDown monitor that rebuilds a dropped key.
#include "MacPeerDroppedKeyRelay.h"

#if JUCE_MAC

#include "MacPeerDroppedKey.h"
#import <AppKit/AppKit.h>

namespace synth {

namespace {

juce::ModifierKeys modifiersOf(NSEvent* event) {
    const auto flags = event.modifierFlags;
    int result = 0;
    if (flags & NSEventModifierFlagControl)
        result |= juce::ModifierKeys::ctrlModifier;
    if (flags & NSEventModifierFlagShift)
        result |= juce::ModifierKeys::shiftModifier;
    if (flags & NSEventModifierFlagOption)
        result |= juce::ModifierKeys::altModifier;
    if (flags & NSEventModifierFlagCommand)
        result |= juce::ModifierKeys::commandModifier;
    return juce::ModifierKeys(result);
}

int firstUnit(NSString* text) { return text.length > 0 ? static_cast<int>([text characterAtIndex:0]) : 0; }

// Returns `event` to let AppKit carry on, nil when the app consumed the recovered key.
NSEvent* relay(NSEvent* event) {
    const auto key = recoverKeyPressDroppedByMacPeer(firstUnit(event.characters),
                                                     firstUnit(event.charactersIgnoringModifiers), modifiersOf(event));
    if (!key.has_value())
        return event;
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    auto* peer = focused != nullptr ? focused->getPeer() : nullptr;
    if (peer == nullptr)
        return event;
    return peer->handleKeyPress(*key) ? nil : event;
}

} // namespace

class MacPeerDroppedKeyRelay::Impl {
public:
    Impl() {
        monitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
                                                        handler:^NSEvent*(NSEvent* event) {
                                                            return relay(event);
                                                        }];
    }

    ~Impl() {
        if (monitor != nil)
            [NSEvent removeMonitor:monitor];
    }

private:
    id monitor = nil;
};

MacPeerDroppedKeyRelay::MacPeerDroppedKeyRelay()
    : impl_(std::make_unique<Impl>()) {}

MacPeerDroppedKeyRelay::~MacPeerDroppedKeyRelay() = default;

} // namespace synth

#endif // JUCE_MAC
