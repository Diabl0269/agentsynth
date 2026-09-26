// Concern: bounce/suspend — detaching and re-attaching the device callback so an offline renderer (a bounce) can take
// the graph over without the device fighting it for blocks.

#include "AudioEngine.h"

// Detaches this engine from the device callback so nothing clocks the graph, and returns true if it
// actually detached (false when it wasn't attached in the first place — Hosted mode, before
// initialise(), after shutdown(), or already suspended). Mirrors exactly how initialise() attached
// it.
bool AudioEngine::suspendDeviceCallback() {
    if (!deviceCallbackAttached_)
        return false;

    // The exact inverse of initialise()'s addAudioCallback. JUCE calls audioDeviceStopped() on us
    // from inside this (which releases the graph's resources) and, importantly, does not return
    // until the device thread is out of our callback — so once this returns, nothing is clocking
    // the graph and an offline renderer may take it over.
    deviceManager.removeAudioCallback(this);
    deviceCallbackAttached_ = false;
    return true;
}

// Undoes suspendDeviceCallback(). A no-op in Hosted mode or if already attached; callers must not
// re-apply the device's sample rate / block size by hand — this re-prepares the graph itself.
void AudioEngine::resumeDeviceCallback() {
    if (isHosted() || deviceCallbackAttached_)
        return;

    // juce::AudioDeviceManager::addAudioCallback calls audioDeviceAboutToStart() on the new
    // callback BEFORE adding it to its list whenever a device is open, so by the time the device
    // thread can reach us the transport is back on the device's sample rate and the graph is
    // prepared for the device's block size. Nothing here re-applies that by hand.
    deviceManager.addAudioCallback(this);
    deviceCallbackAttached_ = true;
}
