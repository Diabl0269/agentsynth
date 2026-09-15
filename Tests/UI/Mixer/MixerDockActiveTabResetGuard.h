#pragma once

// MixerDockActiveTabResetGuard.h -- FRO11 (P9-5). Shared by MixerDockComponentTests.cpp and
// MixerPanelComponentTests.cpp (header-only; not compiled on its own and not registered in
// Tests/CMakeLists.txt).
//
// Any test that calls MixerDockComponent::setActiveTab(Mixer) on a real, non-isolated
// MainComponent persists "bottomDockActiveTab" to the SAME shared on-disk "Agent Synth" settings
// file every MainComponent test instance reads -- ChannelFlowTestFixture.h's own "settings-file
// hygiene" comment explains why (the delegating MainComponent ctor always wires the real
// appProperties, never a per-test-isolated one). Left uncleared, one test's Mixer-tab switch
// survives into every later test in the same binary run (and a real developer's own settings file
// on this machine) that constructs a fresh MainComponent and assumes the "Timeline" default --
// exactly the flake this guard exists to prevent. RAII, clearing the key on construction AND
// destruction, matching ChannelFlowTest::resetKeys()'s before-AND-after shape.
#include "MainComponent/MainComponent.h"

struct MixerDockActiveTabResetGuardMDT {
    MixerDockActiveTabResetGuardMDT() { resetKey(); }
    ~MixerDockActiveTabResetGuardMDT() { resetKey(); }

    static void resetKey() {
        juce::PropertiesFile::Options opts;
        opts.applicationName = "Agent Synth";
        opts.folderName = "Agent Synth";
        opts.filenameSuffix = "settings";
        opts.osxLibrarySubFolder = "Application Support";
        opts.storageFormat = juce::PropertiesFile::storeAsXML;

        juce::ApplicationProperties props;
        props.setStorageParameters(opts);
        if (auto* s = props.getUserSettings()) {
            s->removeValue("bottomDockActiveTab");
            s->saveIfNeeded();
        }
    }
};
