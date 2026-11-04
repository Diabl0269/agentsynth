#pragma once

#include <juce_core/juce_core.h>

#if JUCE_MAC

#include <memory>

namespace synth {

/** Gives the app the key presses JUCE's macOS peer drops (see MacPeerDroppedKey.h): Ctrl+Shift+2
 *  and the other NUL-producing Control chords. A local NSEvent keyDown monitor sees the event before
 *  the peer, rebuilds the KeyPress the peer would have built and hands it to the focused component's
 *  peer, which dispatches it like any other key. Plain C++ header over a PIMPL so Main.cpp needs no
 *  Objective-C++. One per process, held by the application object. */
class MacPeerDroppedKeyRelay {
public:
    MacPeerDroppedKeyRelay();
    ~MacPeerDroppedKeyRelay();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MacPeerDroppedKeyRelay)
};

} // namespace synth

#endif // JUCE_MAC
